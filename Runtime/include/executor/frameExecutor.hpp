#pragma once

#include "camera/iFrameSource.hpp"
#include "core/result.hpp"
#include "executor/iPipelineExecutor.hpp"

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
		if (executor_) {
			executor_->requestStop(mode);
		}
	}

private:
	[[nodiscard]] static core::Result<void> failure(
		core::StatusCode code,
		const char* message) {
		return core::Result<void>::failure(core::Status::error(code, message));
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
			{
				std::lock_guard lock(stateMutex_);
				++summary_.sourceFailures;
			}
			invokeStatusCallback(options_.sourceFailureCallback, frame.status());
			stopForFailure = options_.sourceFailurePolicy == SourceFailurePolicy::Stop;
		} else {
			auto submitted = executor_->submit(
				pipeline::PipelinePacket(std::move(frame).value()),
				[this](TaskId id, const core::Result<ResultType>& result) {
					{
						std::lock_guard lock(stateMutex_);
						++summary_.completed;
						if (!result) {
							++summary_.failed;
						}
					}
					invokeCompletionCallback(id, result);
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
			} else {
				{
					std::lock_guard lock(stateMutex_);
					stopForFailure = !stopRequested_;
				}
				if (stopForFailure) {
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

	static void invokeStatusCallback(
		const std::function<void(const core::Status&)>& callback,
		const core::Status& status) noexcept {
		if (!callback) {
			return;
		}
		try {
			callback(status);
		} catch (...) {
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

	std::mutex stateMutex_;
	FrameExecutionSummary summary_;
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