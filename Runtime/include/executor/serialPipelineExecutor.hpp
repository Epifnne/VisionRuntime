#pragma once

#include "core/completionDispatcher.hpp"
#include "core/result.hpp"
#include "executor/executorOptions.hpp"
#include "executor/executorTask.hpp"
#include "executor/iPipelineExecutor.hpp"
#include "pipeline/iVisionPipeline.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace visionRuntime::executor {

template<typename ResultType>
class SerialPipelineExecutor final : public IPipelineExecutor<ResultType> {
public:
	explicit SerialPipelineExecutor(
		std::unique_ptr<pipeline::IVisionPipeline<ResultType>> pipeline,
		ExecutorOptions options = {})
		: pipeline_(std::move(pipeline)),
		  queueCapacity_(options.queueCapacity),
		  queueFullPolicy_(options.queueFullPolicy),
		  completionDispatcher_(options.queueCapacity) {
		runnerThread_ = std::thread([this] { runTasks(); });
	}

	SerialPipelineExecutor(
		std::unique_ptr<pipeline::IVisionPipeline<ResultType>> pipeline,
		std::size_t queueCapacity)
		: SerialPipelineExecutor(
			std::move(pipeline), ExecutorOptions{queueCapacity}) {}

	~SerialPipelineExecutor() override {
		stop(StopMode::Graceful);
	}

	SerialPipelineExecutor(const SerialPipelineExecutor&) = delete;
	SerialPipelineExecutor& operator=(const SerialPipelineExecutor&) = delete;
	SerialPipelineExecutor(SerialPipelineExecutor&&) = delete;
	SerialPipelineExecutor& operator=(SerialPipelineExecutor&&) = delete;

	[[nodiscard]] core::Result<TaskHandle<ResultType>> submit(
		pipeline::PipelinePacket packet,
		CompletionCallback<ResultType> callback = {}) override {
		std::unique_lock lock(queueMutex_);
		if (!pipeline_) {
			return submitFailure(core::StatusCode::InvalidState,
				"executor requires a pipeline");
		}
		if (queueCapacity_ == 0) {
			return submitFailure(core::StatusCode::InvalidArgument,
				"executor queue capacity must be greater than zero");
		}
		if (!accepting_) {
			return submitFailure(core::StatusCode::InvalidState,
				"executor has stopped accepting tasks");
		}
		if (queueFullPolicy_ == QueueFullPolicy::Block) {
			queueSpaceAvailable_.wait(lock, [this] {
				return !accepting_ || taskQueue_.size() < queueCapacity_;
			});
			if (!accepting_) {
				return submitFailure(core::StatusCode::InvalidState,
					"executor has stopped accepting tasks");
			}
		}
		if (taskQueue_.size() >= queueCapacity_) {
			return submitFailure(core::StatusCode::QueueFull,
				"executor queue reached its capacity");
		}

		const auto taskId = nextTaskId_.fetch_add(1);
		Task task(taskId, std::move(packet), std::move(callback));
		auto handle = task.handle();
		taskQueue_.push_back(std::move(task));
		queueReady_.notify_one();
		return core::Result<TaskHandle<ResultType>>::success(
			std::move(handle));
	}

	void stop(StopMode mode = StopMode::Graceful) noexcept override {
		{
			std::lock_guard lock(queueMutex_);
			accepting_ = false;
			stopRequested_ = true;
			if (mode == StopMode::Immediate) {
				if (runningTask_ != nullptr) {
					runningTask_->requestCancellation();
				}
				for (auto& task : taskQueue_) {
					task.requestCancellation();
				}
			}
		}

		queueReady_.notify_all();
		queueSpaceAvailable_.notify_all();
		joinRunner();
		completionDispatcher_.finish();
		if (pipeline_) {
			pipeline_->finishBatch();
		}
	}

private:
	using Task = ExecutorTask<ResultType>;

	[[nodiscard]] static core::Result<TaskHandle<ResultType>> submitFailure(
		core::StatusCode code,
		const char* message) {
		return core::Result<TaskHandle<ResultType>>::failure(
			core::Status::error(code, message));
	}

	[[nodiscard]] static core::Result<ResultType> cancelledResult(const char* message) {
		return core::Result<ResultType>::failure(
			core::Status::error(core::StatusCode::Cancelled, message));
	}

	void runTasks() noexcept {
		for (;;) {
			std::optional<Task> task;
			{
				std::unique_lock lock(queueMutex_);
				queueReady_.wait(lock, [this] {
					return stopRequested_ || !taskQueue_.empty();
				});
				if (stopRequested_ && taskQueue_.empty()) {
					break;
				}

				task.emplace(std::move(taskQueue_.front()));
				taskQueue_.pop_front();
				runningTask_ = &*task;
			}
			queueSpaceAvailable_.notify_one();

			if (task->cancellationRequested()) {
				queueCompletion(std::move(*task), cancelledResult("task was cancelled"));
			} else {
				task->markRunning();
				auto result = execute(std::move(task->packet()));
				if (task->cancellationRequested()) {
					result = cancelledResult("task was cancelled");
				}
				queueCompletion(std::move(*task), std::move(result));
			}

			std::lock_guard lock(queueMutex_);
			runningTask_ = nullptr;
		}
	}

	[[nodiscard]] core::Result<ResultType> execute(
		pipeline::PipelinePacket packet) noexcept {
		try {
			return pipeline_->run(std::move(packet));
		} catch (const std::exception& exception) {
			return core::Result<ResultType>::failure(core::Status::error(
				core::StatusCode::Internal,
				std::string("pipeline execution threw an exception: ") + exception.what()));
		} catch (...) {
			return core::Result<ResultType>::failure(core::Status::error(
				core::StatusCode::Internal,
				"pipeline execution threw an unknown exception"));
		}
	}

	void queueCompletion(Task task, core::Result<ResultType> result) noexcept {
		auto sharedTask = std::make_shared<Task>(std::move(task));
		static_cast<void>(completionDispatcher_.dispatch(
			std::move(result),
			[sharedTask = std::move(sharedTask)](auto delivered) {
				sharedTask->complete(std::move(delivered));
			}));
	}

	void joinRunner() noexcept {
		std::lock_guard lock(joinMutex_);
		if (runnerThread_.joinable() &&
			runnerThread_.get_id() != std::this_thread::get_id()) {
			runnerThread_.join();
		}
	}

	std::unique_ptr<pipeline::IVisionPipeline<ResultType>> pipeline_;
	const std::size_t queueCapacity_;
	const QueueFullPolicy queueFullPolicy_;
	std::atomic<TaskId> nextTaskId_{1};
	core::CompletionDispatcher<ResultType> completionDispatcher_;

	std::mutex queueMutex_;
	std::condition_variable queueReady_;
	std::condition_variable queueSpaceAvailable_;
	std::deque<Task> taskQueue_;
	Task* runningTask_ = nullptr;
	bool accepting_ = true;
	bool stopRequested_ = false;
	std::thread runnerThread_;
	std::mutex joinMutex_;
};

} // namespace visionRuntime::executor