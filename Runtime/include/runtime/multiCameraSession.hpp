#pragma once

#include "camera/iFrameSource.hpp"
#include "camera/sourceFault.hpp"
#include "core/result.hpp"
#include "executor/iPipelineExecutor.hpp"
#include "executor/taskHandle.hpp"
#include "executor/frameOutcome.hpp"
#include "executor/frameObservers.hpp"
#include "executor/executorOptions.hpp"
#include "logs/logger.hpp"
#include "pipeline/pipelinePacket.hpp"
#include "vision/frame.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace visionRuntime::runtime {

struct MultiCameraExecutionSummary {
	std::size_t received = 0;
	std::size_t submitted = 0;
	std::size_t completed = 0;
	std::size_t failed = 0;
	std::size_t dropped = 0;
	std::size_t sourceFailures = 0;
	std::vector<std::size_t> droppedPerSource;
	std::vector<std::size_t> receivedPerSource;
	std::uint64_t missingFrames = 0;
	std::vector<std::uint64_t> missingPerSource;
	std::size_t rejected = 0;
	std::size_t expired = 0;
	std::string lastError{};
};

template<typename ResultType>
struct MultiCameraExecutionOptions {
	std::optional<std::chrono::milliseconds> duration;
	std::function<void(const core::Status&)> sourceFailureCallback;
	std::function<void(const core::Status&)> droppedFrameCallback;
	/// Invoked with the source id and the incoming frame before the packet is
	/// submitted to the executor. Implementations may keep a second Frame view
	/// over the same shared buffer; the callback runs on the source thread and
	/// must stay non-blocking.
	std::function<void(std::uint32_t, const vision::Frame&)> frameObserver;
	executor::FaultPolicy faultPolicy = executor::FaultPolicy::ReportAndContinue;
	executor::FrameOutcomeCallback<ResultType> resultObserver;
	std::function<void(const camera::SourceFault&)> sourceFaultObserver;
};

/**
 * Drives multiple frame sources into one pipeline executor. Each source is
 * assigned a stable sourceId (its index) which is stamped onto every
 * PipelinePacket, so downstream stages and statistics can attribute frames
 * to their camera.
 */
template<typename ResultType>
class MultiCameraSession {
public:
	MultiCameraSession(
		std::vector<std::unique_ptr<camera::IFrameSource>> sources,
		std::unique_ptr<executor::IPipelineExecutor<ResultType>> executor,
		MultiCameraExecutionOptions<ResultType> options = {})
		: sources_(std::move(sources)),
		  executor_(std::move(executor)),
		  options_(std::move(options)),
		  summary_{.droppedPerSource = std::vector<std::size_t>(sources_.size()),
			.receivedPerSource = std::vector<std::size_t>(sources_.size()),
			.missingPerSource = std::vector<std::uint64_t>(sources_.size())},
		  sequences_(sources_.size()) {}

	MultiCameraSession(const MultiCameraSession&) = delete;
	MultiCameraSession& operator=(const MultiCameraSession&) = delete;
	MultiCameraSession(MultiCameraSession&&) = delete;
	MultiCameraSession& operator=(MultiCameraSession&&) = delete;

	[[nodiscard]] core::Result<void> addObservers(
		executor::FrameObservers<ResultType> observers) {
		std::lock_guard lock(stateMutex_);
		if (started_) {
			return failure(core::StatusCode::InvalidState,
				"observers must be attached before the runtime starts");
		}
		observers_.push_back(std::move(observers));
		return core::Result<void>::success();
	}

	~MultiCameraSession() {
		requestStop(executor::StopMode::Immediate);
		static_cast<void>(wait());
	}

	[[nodiscard]] core::Result<void> start() {
		if (sources_.empty()) {
			return failure(core::StatusCode::InvalidArgument,
				"multi camera session requires at least one source");
		}
		if (!executor_) {
			return failure(core::StatusCode::InvalidState,
				"multi camera session requires a pipeline executor");
		}
		if (options_.duration && options_.duration->count() <= 0) {
			return failure(core::StatusCode::InvalidArgument,
				"execution duration must be greater than zero");
		}
		{
			std::lock_guard lock(stateMutex_);
			if (started_) {
				return failure(core::StatusCode::InvalidState,
					"multi camera session has already started");
			}
			started_ = true;
		}

		if (options_.duration) {
			timerThread_ = std::thread([this, duration = *options_.duration] {
				std::unique_lock lock(timerMutex_);
				if (!timerReady_.wait_for(lock, duration,
					[this] { return timerCancelled_; })) {
					lock.unlock();
					requestStop();
				}
			});
		}

		for (std::size_t index = 0; index < sources_.size(); ++index) {
			if (stopRequested()) {
				break;
			}
			if (!sources_[index]) {
				requestStop(executor::StopMode::Immediate);
				rollbackStartedSources(index);
				return failure(core::StatusCode::InvalidArgument,
					"multi camera session contains a null source");
			}
			auto started = sources_[index]->start(
				[this, index](core::Result<vision::Frame> frame) {
					onFrame(index, std::move(frame));
				});
			if (!started) {
				requestStop(executor::StopMode::Immediate);
				rollbackStartedSources(index);
				cancelTimer();
				joinTimer();
				logs::report(started.status());
				return started;
			}
		}
		return core::Result<void>::success();
	}

	[[nodiscard]] MultiCameraExecutionSummary wait() {
		{
			std::lock_guard lock(waitMutex_);
			bool finished;
			{
				std::lock_guard stateLock(stateMutex_);
				finished = finished_;
			}
			if (!finished) {
				for (auto& source : sources_) {
					if (source) {
						source->wait();
					}
				}
				requestStop();
				if (executor_) {
					executor_->requestStop();
					executor_->wait();
				}
				joinTimer();
				std::lock_guard stateLock(stateMutex_);
				finished_ = true;
			}
		}
		std::lock_guard lock(stateMutex_);
		return summary_;
	}

	/// Live counters while the run is in flight (and after wait()). Callers
	/// other than the control thread must use this instead of wait().
	[[nodiscard]] MultiCameraExecutionSummary currentSummary() const {
		std::lock_guard lock(stateMutex_);
		return summary_;
	}

	[[nodiscard]] bool stopRequested() const {
		std::lock_guard lock(stateMutex_);
		return stopRequested_;
	}

	void requestStop(
		executor::StopMode mode = executor::StopMode::Graceful) noexcept {
		{
			std::lock_guard lock(stateMutex_);
			if (finished_) {
				return;
			}
			stopRequested_ = true;
		}
		cancelTimer();
		for (auto& source : sources_) {
			if (source) {
				source->requestStop();
			}
		}
		if (executor_ && mode == executor::StopMode::Immediate) {
			executor_->requestStop(mode);
		}
	}

	[[nodiscard]] const std::vector<std::unique_ptr<camera::IFrameSource>>&
	sources() const noexcept {
		return sources_;
	}

private:
	[[nodiscard]] static core::Result<void> failure(
		core::StatusCode code,
		const char* message) {
		auto status = core::Status::error(code, message);
		logs::report(status);
		return core::Result<void>::failure(std::move(status));
	}

	void rollbackStartedSources(std::size_t count) noexcept {
		for (std::size_t index = 0; index < count; ++index) {
			sources_[index]->requestStop();
			sources_[index]->wait();
		}
	}

	void onFrame(std::size_t sourceIndex, core::Result<vision::Frame> frame) noexcept {
		{
			std::lock_guard lock(stateMutex_);
			++summary_.received;
			++summary_.receivedPerSource[sourceIndex];
		}

		if (!frame) {
			bool stopping;
			{
				std::lock_guard lock(stateMutex_);
				++summary_.sourceFailures;
				summary_.lastError = frame.status().toString();
				stopping = stopRequested_;
			}
			if (!stopping) {
				logs::report(frame.status());
			}
			invokeStatusCallback(options_.sourceFailureCallback, frame.status());
			const bool terminal =
				sources_[sourceIndex]->failureIsTerminal(frame.status());
			publishSourceFault({
				.sourceId = static_cast<std::uint32_t>(sourceIndex),
				.status = frame.status(),
				.terminal = terminal});
			if (!stopping && (terminal ||
				options_.faultPolicy == executor::FaultPolicy::StopSession)) {
				requestStop(executor::StopMode::Immediate);
			}
			return;
		}

		const auto sequence = frame->metadata().sequenceNumber;
		const auto previous = sequences_[sourceIndex].previous();
		const auto missing = sequences_[sourceIndex].observe(sequence);
		if (missing > 0) {
			auto status = core::Status::error(core::StatusCode::DataLoss,
				"acquisition frame sequence contains a gap");
			{
				std::lock_guard lock(stateMutex_);
				summary_.missingFrames += missing;
				summary_.missingPerSource[sourceIndex] += missing;
				summary_.lastError = status.toString();
			}
			logs::report(status);
			publishSourceFault({
				.sourceId = static_cast<std::uint32_t>(sourceIndex),
				.status = std::move(status),
				.missingFrames = missing,
				.previousSequence = previous,
				.currentSequence = sequence});
			if (options_.faultPolicy == executor::FaultPolicy::StopSession) {
				requestStop(executor::StopMode::Immediate);
			}
		}

		if (options_.frameObserver) {
			try {
				options_.frameObserver(
					static_cast<std::uint32_t>(sourceIndex), frame.value());
			} catch (...) {
				logs::error("frame observer threw an exception");
			}
		}

		for (const auto& observer : observers_) {
			if (observer.frame) {
				try {
					observer.frame(static_cast<std::uint32_t>(sourceIndex), frame.value());
				} catch (...) {
					logs::error("attached frame observer threw an exception");
				}
			}
		}
		pipeline::PipelinePacket packet(std::move(frame).value(), {},
			static_cast<std::uint32_t>(sourceIndex));
		const auto identity = packet.identity();
		auto submitted = executor_->submit(std::move(packet),
			[this, identity](executor::TaskId id, const core::Result<ResultType>& result) {
				{
					std::lock_guard lock(stateMutex_);
					++summary_.completed;
					if (!result) {
						++summary_.failed;
						if (result.status().code() != core::StatusCode::Cancelled) {
							summary_.lastError = result.status().toString();
						}
						if (result.status().code() == core::StatusCode::DeadlineExceeded) {
							++summary_.expired;
						}
					}
				}
				publishResult(executor::FrameOutcome::Completed, identity, id, result);
				if (!result && result.status().code() != core::StatusCode::Cancelled) {
					logs::report(result.status());
					if (options_.faultPolicy == executor::FaultPolicy::StopSession) {
						requestStop(executor::StopMode::Immediate);
					}
				}
			});
		if (submitted) {
			std::lock_guard lock(stateMutex_);
			++summary_.submitted;
		} else if (submitted.status().code() == core::StatusCode::QueueFull) {
			{
				std::lock_guard lock(stateMutex_);
				++summary_.dropped;
				++summary_.droppedPerSource[sourceIndex];
			}
			invokeStatusCallback(options_.droppedFrameCallback, submitted.status());
			publishResult(executor::FrameOutcome::Dropped, identity, std::nullopt,
				core::Result<ResultType>::failure(submitted.status()));
		} else {
			bool shouldStop;
			{
				std::lock_guard lock(stateMutex_);
				shouldStop = !stopRequested_;
				++summary_.rejected;
				if (!stopRequested_) {
					summary_.lastError = submitted.status().toString();
				}
				if (submitted.status().code() == core::StatusCode::DeadlineExceeded) {
					++summary_.expired;
				}
			}
			publishResult(executor::FrameOutcome::Rejected, identity, std::nullopt,
				core::Result<ResultType>::failure(submitted.status()));
			if (shouldStop) {
				logs::report(submitted.status());
				invokeStatusCallback(
					options_.sourceFailureCallback, submitted.status());
				if (submitted.status().code() != core::StatusCode::DeadlineExceeded ||
					options_.faultPolicy == executor::FaultPolicy::StopSession) {
					requestStop(executor::StopMode::Immediate);
				}
			}

		}
	}

	void publishResult(executor::FrameOutcome outcome,
		const pipeline::FrameIdentity& identity,
		std::optional<executor::TaskId> taskId,
		const core::Result<ResultType>& result) noexcept {
		if (options_.resultObserver) {
			try {
				options_.resultObserver(outcome, identity, taskId, result);
			} catch (...) {
				logs::error("result observer threw an exception");
			}
		}
		for (const auto& observer : observers_) {
			if (observer.result) {
				try {
					observer.result(outcome, identity, taskId, result);
				} catch (...) {
					logs::error("attached result observer threw an exception");
				}
			}
		}
	}

	void publishSourceFault(const camera::SourceFault& fault) noexcept {
		if (options_.sourceFaultObserver) {
			try {
				options_.sourceFaultObserver(fault);
			} catch (...) {
				logs::error("source fault observer threw an exception");
			}
		}
		for (const auto& observer : observers_) {
			if (observer.sourceFault) {
				try {
					observer.sourceFault(fault);
				} catch (...) {
					logs::error("attached source fault observer threw an exception");
				}
			}
		}
	}

	static void invokeStatusCallback(
		const std::function<void(const core::Status&)>& callback,
		const core::Status& status) noexcept {
		if (!callback) {
			return;
		}
		try {
			callback(status);
		} catch (...) {
			logs::error("source status callback threw an exception");
		}
	}

	void cancelTimer() noexcept {
		{
			std::lock_guard lock(timerMutex_);
			timerCancelled_ = true;
		}
		timerReady_.notify_all();
	}

	void joinTimer() noexcept {
		if (timerThread_.joinable() &&
			timerThread_.get_id() != std::this_thread::get_id()) {
			timerThread_.join();
		}
	}

	std::vector<std::unique_ptr<camera::IFrameSource>> sources_;
	std::unique_ptr<executor::IPipelineExecutor<ResultType>> executor_;
	MultiCameraExecutionOptions<ResultType> options_;
	std::vector<executor::FrameObservers<ResultType>> observers_;
	MultiCameraExecutionSummary summary_;
	std::vector<camera::FrameSequenceTracker> sequences_;

	mutable std::mutex stateMutex_;
	bool started_ = false;
	bool finished_ = false;
	bool stopRequested_ = false;

	std::mutex timerMutex_;
	std::condition_variable timerReady_;
	bool timerCancelled_ = false;
	std::thread timerThread_;

	std::mutex waitMutex_;
};

} // namespace visionRuntime::runtime
