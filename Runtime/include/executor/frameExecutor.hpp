#pragma once

#include "camera/iFrameSource.hpp"
#include "camera/sourceFault.hpp"
#include "core/result.hpp"
#include "executor/iPipelineExecutor.hpp"
#include "executor/frameOutcome.hpp"
#include "executor/frameObservers.hpp"
#include "executor/executorOptions.hpp"
#include "logs/logger.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace visionRuntime::executor {

enum class SourceFailurePolicy {
	Skip,
	Stop
};

struct FrameExecutionSummary {
	std::size_t received = 0;
	std::size_t submitted = 0;
	std::size_t completed = 0;
	std::size_t failed = 0;
	std::size_t dropped = 0;
	std::size_t sourceFailures = 0;
	std::uint64_t missingFrames = 0;
	std::size_t rejected = 0;
	std::size_t expired = 0;
	std::string lastError{};
};

template<typename ResultType>
struct FrameExecutionOptions {
	std::optional<std::size_t> frameCount;
	std::optional<std::chrono::milliseconds> duration;
	std::chrono::milliseconds frameInterval{0};
	SourceFailurePolicy sourceFailurePolicy = SourceFailurePolicy::Skip;
	CompletionCallback<ResultType> completionCallback;
	std::function<void(const core::Status&)> sourceFailureCallback;
	std::function<void(const core::Status&)> droppedFrameCallback;
	FaultPolicy faultPolicy = FaultPolicy::ReportAndContinue;
	FrameOutcomeCallback<ResultType> resultObserver;
	std::function<void(const camera::SourceFault&)> sourceFaultObserver;
};

template<typename ResultType>
class FrameExecutor {
public:
	FrameExecutor(
		std::unique_ptr<camera::IFrameSource> source,
		std::unique_ptr<IPipelineExecutor<ResultType>> executor,
		FrameExecutionOptions<ResultType> options = {})
		: source_(std::move(source)),
		  executor_(std::move(executor)),
		  options_(std::move(options)) {}

	~FrameExecutor() {
		requestStop(StopMode::Immediate);
		static_cast<void>(wait());
	}

	FrameExecutor(const FrameExecutor&) = delete;
	FrameExecutor& operator=(const FrameExecutor&) = delete;
	FrameExecutor(FrameExecutor&&) = delete;
	FrameExecutor& operator=(FrameExecutor&&) = delete;

	[[nodiscard]] core::Result<void> addObservers(FrameObservers<ResultType> observers) {
		std::lock_guard lock(stateMutex_);
		if (started_) {
			return failure(core::StatusCode::InvalidState,
				"observers must be attached before the runtime starts");
		}
		observers_.push_back(std::move(observers));
		return core::Result<void>::success();
	}

	[[nodiscard]] FrameExecutionSummary currentSummary() const {
		std::lock_guard lock(stateMutex_);
		return summary_;
	}

	[[nodiscard]] bool stopRequested() const {
		std::lock_guard lock(stateMutex_);
		return stopRequested_;
	}

	[[nodiscard]] camera::IFrameSource* source() const noexcept { return source_.get(); }

	[[nodiscard]] core::Result<void> start() {
		if (!source_) {
			return failure(core::StatusCode::InvalidState,
				"frame executor requires a source");
		}
		if (!executor_) {
			return failure(core::StatusCode::InvalidState,
				"frame executor requires a pipeline executor");
		}
		if (options_.frameCount && *options_.frameCount == 0) {
			return failure(core::StatusCode::InvalidArgument,
				"frame count must be greater than zero");
		}
		if (options_.duration && options_.duration->count() <= 0) {
			return failure(core::StatusCode::InvalidArgument,
				"execution duration must be greater than zero");
		}
		if (options_.frameInterval.count() < 0) {
			return failure(core::StatusCode::InvalidArgument,
				"frame interval must not be negative");
		}

		{
			std::lock_guard lock(stateMutex_);
			if (started_) {
				return failure(core::StatusCode::InvalidState,
					"frame executor has already started");
			}
			started_ = true;
		}

		if (options_.duration) {
			timerThread_ = std::thread([this, duration = *options_.duration] {
				std::unique_lock lock(timerMutex_);
				if (!timerReady_.wait_for(lock, duration, [this] { return timerCancelled_; })) {
					lock.unlock();
					requestStop();
				}
			});
		}

		auto started = source_->start([this](core::Result<vision::Frame> frame) {
			onFrame(std::move(frame));
		});
		if (!started) {
			requestStop(StopMode::Immediate);
			static_cast<void>(wait());
			logs::report(started.status());
			return started;
		}
		return core::Result<void>::success();
	}

	[[nodiscard]] FrameExecutionSummary wait() {
		{
			std::lock_guard lock(waitMutex_);
			bool finished;
			{
				std::lock_guard stateLock(stateMutex_);
				finished = finished_;
			}
			if (!finished) {
				if (source_) {
					source_->wait();
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

	void requestStop(StopMode mode = StopMode::Graceful) noexcept {
		{
			std::lock_guard lock(stateMutex_);
			if (finished_) {
				return;
			}
			stopRequested_ = true;
		}

		cancelTimer();
		if (source_) {
			source_->requestStop();
		}
		if (executor_ && mode == StopMode::Immediate) {
			executor_->requestStop(mode);
		}
	}

private:
	[[nodiscard]] static core::Result<void> failure(
		core::StatusCode code,
		const char* message) {
		auto status = core::Status::error(code, message);
		logs::report(status);
		return core::Result<void>::failure(std::move(status));
	}

	void onFrame(core::Result<vision::Frame> frame) noexcept {
		bool reachedFrameCount = false;
		{
			std::lock_guard lock(stateMutex_);
			++summary_.received;
			reachedFrameCount = options_.frameCount &&
				summary_.received >= *options_.frameCount;
		}

		bool stopForFailure = false;
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
			const bool terminal = source_->failureIsTerminal(frame.status());
			publishSourceFault({.status = frame.status(), .terminal = terminal});
			stopForFailure = terminal ||
				options_.sourceFailurePolicy == SourceFailurePolicy::Stop ||
				options_.faultPolicy == FaultPolicy::StopSession;
		} else {
			const auto sequence = frame->metadata().sequenceNumber;
			const auto previous = sequence_.previous();
			const auto missing = sequence_.observe(sequence);
			if (missing > 0) {
				{
					std::lock_guard lock(stateMutex_);
					summary_.missingFrames += missing;
					summary_.lastError = "DataLoss: acquisition frame sequence contains a gap";
				}
				auto status = core::Status::error(core::StatusCode::DataLoss,
					"acquisition frame sequence contains a gap");
				logs::report(status);
				publishSourceFault({.status = std::move(status),
					.missingFrames = missing, .previousSequence = previous,
					.currentSequence = sequence});
				if (options_.faultPolicy == FaultPolicy::StopSession) {
					requestStop(StopMode::Immediate);
				}
			}
			for (const auto& observer : observers_) {
				if (observer.frame) {
					try {
						observer.frame(0, frame.value());
					} catch (...) {
						logs::error("runtime frame observer threw an exception");
					}
				}
			}
			pipeline::PipelinePacket packet(std::move(frame).value(), {}, 0);
			const auto identity = packet.identity();
			auto submitted = executor_->submit(std::move(packet),
				[this, identity](TaskId id, const core::Result<ResultType>& result) {
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
					invokeCompletionCallback(id, result);
					publishResult(FrameOutcome::Completed, identity, id, result);
					if (!result && result.status().code() != core::StatusCode::Cancelled) {
						logs::report(result.status());
						if (options_.faultPolicy == FaultPolicy::StopSession) {
							requestStop(StopMode::Immediate);
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
				}
				invokeStatusCallback(
					options_.droppedFrameCallback, submitted.status());
				publishResult(FrameOutcome::Dropped, identity, std::nullopt,
					core::Result<ResultType>::failure(submitted.status()));
			} else {
				{
					std::lock_guard lock(stateMutex_);
					++summary_.rejected;
					if (!stopRequested_) {
						summary_.lastError = submitted.status().toString();
					}
					const bool expired =
						submitted.status().code() == core::StatusCode::DeadlineExceeded;
					if (expired) {
						++summary_.expired;
					}
					stopForFailure = !stopRequested_ &&
						(!expired || options_.faultPolicy == FaultPolicy::StopSession);
				}
				publishResult(FrameOutcome::Rejected, identity, std::nullopt,
					core::Result<ResultType>::failure(submitted.status()));
				logs::report(submitted.status());
				if (stopForFailure) {
					logs::report(submitted.status());
					invokeStatusCallback(
						options_.sourceFailureCallback, submitted.status());
				}

			}
		}

		if (options_.frameInterval.count() > 0 &&
			!reachedFrameCount && !stopForFailure) {
			std::this_thread::sleep_for(options_.frameInterval);
		}
		if (reachedFrameCount || stopForFailure) {
			requestStop(stopForFailure ? StopMode::Immediate : StopMode::Graceful);
		}
	}

	void publishResult(FrameOutcome outcome,
		const pipeline::FrameIdentity& identity, std::optional<TaskId> taskId,
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

	void invokeCompletionCallback(
		TaskId id,
		const core::Result<ResultType>& result) noexcept {
		if (!options_.completionCallback) {
			return;
		}
		try {
			options_.completionCallback(id, result);
		} catch (...) {
			logs::error("frame completion callback threw an exception");
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
		std::lock_guard lock(timerJoinMutex_);
		if (timerThread_.joinable() &&
			timerThread_.get_id() != std::this_thread::get_id()) {
			timerThread_.join();
		}
	}

	std::unique_ptr<camera::IFrameSource> source_;
	std::unique_ptr<IPipelineExecutor<ResultType>> executor_;
	FrameExecutionOptions<ResultType> options_;

	mutable std::mutex stateMutex_;
	std::vector<FrameObservers<ResultType>> observers_;
	FrameExecutionSummary summary_;
	camera::FrameSequenceTracker sequence_;
	bool started_ = false;
	bool stopRequested_ = false;
	bool finished_ = false;
	std::mutex waitMutex_;

	std::thread timerThread_;
	std::mutex timerMutex_;
	std::condition_variable timerReady_;
	bool timerCancelled_ = false;
	std::mutex timerJoinMutex_;
};

} // namespace visionRuntime::executor