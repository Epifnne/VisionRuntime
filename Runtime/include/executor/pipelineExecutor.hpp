#pragma once

#include "core/result.hpp"
#include "executor/executorOptions.hpp"
#include "executor/taskHandle.hpp"
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

namespace visonRuntime::executor {

enum class StopMode {
	Graceful,
	Immediate
};

template<typename ResultType>
class PipelineExecutor {
public:
	explicit PipelineExecutor(
		std::unique_ptr<pipeline::IVisionPipeline<ResultType>> pipeline,
		ExecutorOptions options = {})
		: pipeline_(std::move(pipeline)), queueCapacity_(options.queueCapacity) {
		runnerThread_ = std::thread([this] { runTasks(); });
		completionThread_ = std::thread([this] { dispatchCompletions(); });
	}

	PipelineExecutor(
		std::unique_ptr<pipeline::IVisionPipeline<ResultType>> pipeline,
		std::size_t queueCapacity)
		: PipelineExecutor(
			std::move(pipeline), ExecutorOptions{queueCapacity}) {}

	~PipelineExecutor() {
		stop(StopMode::Graceful);
	}

	PipelineExecutor(const PipelineExecutor&) = delete;
	PipelineExecutor& operator=(const PipelineExecutor&) = delete;
	PipelineExecutor(PipelineExecutor&&) = delete;
	PipelineExecutor& operator=(PipelineExecutor&&) = delete;

	[[nodiscard]] core::Result<TaskHandle<ResultType>> submit(
		pipeline::PipelinePacket packet,
		CompletionCallback<ResultType> callback = {}) {
		std::lock_guard lock(queueMutex_);
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
		if (taskQueue_.size() >= queueCapacity_) {
			return submitFailure(core::StatusCode::QueueFull,
				"executor queue reached its capacity");
		}

		const auto taskId = nextTaskId_.fetch_add(1);
		auto state = std::make_shared<detail::TaskSharedState<ResultType>>();
		taskQueue_.push_back(Task{
			taskId, std::move(packet), std::move(callback), state});
		queueReady_.notify_one();
		return core::Result<TaskHandle<ResultType>>::success(
			TaskHandle<ResultType>(taskId, std::move(state)));
	}

	void stop(StopMode mode = StopMode::Graceful) noexcept {
		{
			std::lock_guard lock(queueMutex_);
			accepting_ = false;
			stopRequested_ = true;
			if (mode == StopMode::Immediate) {
				if (runningTask_) {
					runningTask_->cancelRequested.store(true);
				}
				for (auto& task : taskQueue_) {
					task.state->cancelRequested.store(true);
				}
			}
		}

		queueReady_.notify_all();
		joinThreads();
	}

private:
	struct Task {
		TaskId id;
		pipeline::PipelinePacket packet;
		CompletionCallback<ResultType> callback;
		std::shared_ptr<detail::TaskSharedState<ResultType>> state;
	};

	struct Completion {
		Task task;
		core::Result<ResultType> result;
	};

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
				runningTask_ = task->state;
			}

			if (task->state->cancelRequested.load()) {
				queueCompletion(std::move(*task), cancelledResult("task was cancelled"));
			} else {
				task->state->state.store(TaskState::Running);
				auto result = execute(std::move(task->packet));
				if (task->state->cancelRequested.load()) {
					result = cancelledResult("task was cancelled");
				}
				queueCompletion(std::move(*task), std::move(result));
			}

			std::lock_guard lock(queueMutex_);
			runningTask_.reset();
		}

		{
			std::lock_guard lock(completionMutex_);
			runnerFinished_ = true;
		}
		completionReady_.notify_all();
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
		{
			std::lock_guard lock(completionMutex_);
			completionQueue_.push_back(Completion{std::move(task), std::move(result)});
		}
		completionReady_.notify_one();
	}

	void dispatchCompletions() noexcept {
		for (;;) {
			std::optional<Completion> completion;
			{
				std::unique_lock lock(completionMutex_);
				completionReady_.wait(lock, [this] {
					return runnerFinished_ || !completionQueue_.empty();
				});
				if (runnerFinished_ && completionQueue_.empty()) {
					break;
				}
				completion.emplace(std::move(completionQueue_.front()));
				completionQueue_.pop_front();
			}

			auto& state = completion->task.state;
			if (completion->result) {
				state->state.store(TaskState::Completed);
			} else if (completion->result.status().code() == core::StatusCode::Cancelled) {
				state->state.store(TaskState::Cancelled);
			} else {
				state->state.store(TaskState::Failed);
			}
			state->promise.set_value(std::move(completion->result));

			if (completion->task.callback) {
				try {
					completion->task.callback(
						completion->task.id, state->future.get());
				} catch (...) {
				}
			}
		}
	}

	void joinThreads() noexcept {
		std::lock_guard lock(joinMutex_);
		if (runnerThread_.joinable()) {
			if (runnerThread_.get_id() != std::this_thread::get_id()) {
				runnerThread_.join();
			}
		}
		if (completionThread_.joinable()) {
			if (completionThread_.get_id() != std::this_thread::get_id()) {
				completionThread_.join();
			}
		}
	}

	std::unique_ptr<pipeline::IVisionPipeline<ResultType>> pipeline_;
	const std::size_t queueCapacity_;
	std::atomic<TaskId> nextTaskId_{1};

	std::mutex queueMutex_;
	std::condition_variable queueReady_;
	std::deque<Task> taskQueue_;
	std::shared_ptr<detail::TaskSharedState<ResultType>> runningTask_;
	bool accepting_ = true;
	bool stopRequested_ = false;
	std::thread runnerThread_;
	std::mutex joinMutex_;

	std::mutex completionMutex_;
	std::condition_variable completionReady_;
	std::deque<Completion> completionQueue_;
	bool runnerFinished_ = false;
	std::thread completionThread_;
};

} // namespace visonRuntime::executor