#pragma once

#include "camera/iFrameSource.hpp"
#include "core/result.hpp"
#include "executor/executorOptions.hpp"
#include "executor/pipelineExecutor.hpp"
#include "pipeline/iVisionPipeline.hpp"

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
	ExecutorOptions executor;
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
		std::unique_ptr<pipeline::IVisionPipeline<ResultType>> pipeline,
		FrameExecutionOptions<ResultType> options = {})
		: source_(std::move(source)),
		  executor_(std::move(pipeline), options.executor),
		  options_(std::move(options)) {}

	~FrameExecutor() {
		stop();
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
					stop();
				}
			});
		}

		auto started = source_->start([this](core::Result<vision::Frame> frame) {
			onFrame(std::move(frame));
		});
		if (!started) {
			finishWithoutSource();
			return started;
		}
		return core::Result<void>::success();
	}

	[[nodiscard]] FrameExecutionSummary wait() {
		{
			std::unique_lock lock(stateMutex_);
			finishedReady_.wait(lock, [this] { return finished_; });
		}
		joinTimer();
		std::lock_guard lock(stateMutex_);
		return summary_;
	}

	void stop() noexcept {
		std::unique_lock stopLock(stopMutex_);
		{
			std::lock_guard lock(stateMutex_);
			if (finished_) {
				stopLock.unlock();
				cancelTimer();
				joinTimer();
				return;
			}
		}

		cancelTimer();
		if (source_) {
			static_cast<void>(source_->stop());
		}
		executor_.stop(StopMode::Graceful);
		{
			std::lock_guard lock(stateMutex_);
			finished_ = true;
		}
		finishedReady_.notify_all();
		stopLock.unlock();
		joinTimer();
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
			auto submitted = executor_.submit(
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
				stopForFailure = true;
				invokeStatusCallback(
					options_.sourceFailureCallback, submitted.status());
			}
		}

		if (options_.frameInterval.count() > 0 &&
			!reachedFrameCount && !stopForFailure) {
			std::this_thread::sleep_for(options_.frameInterval);
		}
		if (reachedFrameCount || stopForFailure) {
			stop();
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

	void finishWithoutSource() noexcept {
		cancelTimer();
		executor_.stop(StopMode::Graceful);
		{
			std::lock_guard lock(stateMutex_);
			finished_ = true;
		}
		finishedReady_.notify_all();
		joinTimer();
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
	PipelineExecutor<ResultType> executor_;
	FrameExecutionOptions<ResultType> options_;

	std::mutex stateMutex_;
	std::condition_variable finishedReady_;
	FrameExecutionSummary summary_;
	bool started_ = false;
	bool finished_ = false;
	std::mutex stopMutex_;

	std::thread timerThread_;
	std::mutex timerMutex_;
	std::condition_variable timerReady_;
	bool timerCancelled_ = false;
	std::mutex timerJoinMutex_;
};

} // namespace visionRuntime::executor