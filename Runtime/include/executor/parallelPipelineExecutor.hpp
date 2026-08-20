#pragma once

#include "common/boundedBlockingQueue.hpp"
#include "core/completionDispatcher.hpp"
#include "core/result.hpp"
#include "executor/executorOptions.hpp"
#include "executor/executorTask.hpp"
#include "executor/iPipelineExecutor.hpp"
#include "pipeline/iStagedVisionPipeline.hpp"

#include <atomic>
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
class ParallelPipelineExecutor final : public IPipelineExecutor<ResultType> {
public:
	explicit ParallelPipelineExecutor(
		std::unique_ptr<pipeline::IStagedVisionPipeline<ResultType>> pipeline,
		ExecutorOptions options = {})
		: pipeline_(std::move(pipeline)),
		  queueCapacity_(options.queueCapacity),
		  queueFullPolicy_(options.queueFullPolicy),
		  stageQueueCapacity_(options.stageQueueCapacity),
		  preprocessQueue_(options.stageQueueCapacity),
		  inferenceQueue_(options.stageQueueCapacity),
		  completionDispatcher_(options.stageQueueCapacity) {
		preprocessThread_ = std::thread([this] { runPreprocess(); });
		inferenceThread_ = std::thread([this] { runInference(); });
		postprocessThread_ = std::thread([this] { runPostprocess(); });
	}

	~ParallelPipelineExecutor() override {
		stop(StopMode::Graceful);
	}

	ParallelPipelineExecutor(const ParallelPipelineExecutor&) = delete;
	ParallelPipelineExecutor& operator=(const ParallelPipelineExecutor&) = delete;
	ParallelPipelineExecutor(ParallelPipelineExecutor&&) = delete;
	ParallelPipelineExecutor& operator=(ParallelPipelineExecutor&&) = delete;

	[[nodiscard]] core::Result<TaskHandle<ResultType>> submit(
		pipeline::PipelinePacket packet,
		CompletionCallback<ResultType> callback = {}) override {
		std::unique_lock lock(entryMutex_);
		if (!pipeline_) {
			return submitFailure(core::StatusCode::InvalidState,
				"parallel executor requires a staged pipeline");
		}
		if (queueCapacity_ == 0 || stageQueueCapacity_ == 0) {
			return submitFailure(core::StatusCode::InvalidArgument,
				"executor queue capacities must be greater than zero");
		}
		if (!accepting_) {
			return submitFailure(core::StatusCode::InvalidState,
				"executor has stopped accepting tasks");
		}
		if (queueFullPolicy_ == QueueFullPolicy::Block) {
			entrySpaceAvailable_.wait(lock, [this] {
				return !accepting_ || entryQueue_.size() < queueCapacity_;
			});
			if (!accepting_) {
				return submitFailure(core::StatusCode::InvalidState,
					"executor has stopped accepting tasks");
			}
		}
		if (entryQueue_.size() >= queueCapacity_) {
			return submitFailure(core::StatusCode::QueueFull,
				"executor queue reached its capacity");
		}

		const auto taskId = nextTaskId_.fetch_add(1);
		Task task(taskId, std::move(packet), std::move(callback));
		auto handle = task.handle();
		entryQueue_.push_back(std::move(task));
		entryReady_.notify_one();
		return core::Result<TaskHandle<ResultType>>::success(
			std::move(handle));
	}

	void stop(StopMode mode = StopMode::Graceful) noexcept override {
		{
			std::lock_guard lock(entryMutex_);
			accepting_ = false;
			stopRequested_ = true;
			if (mode == StopMode::Immediate) {
				immediateStopRequested_.store(true);
				for (auto& task : entryQueue_) {
					task.requestCancellation();
				}
			}
		}
		entryReady_.notify_all();
		entrySpaceAvailable_.notify_all();
		joinThreads();
		completionDispatcher_.finish();
		if (pipeline_) {
			pipeline_->finishBatch();
		}
	}

private:
	using Task = ExecutorTask<ResultType>;

	struct PreprocessedTask {
		Task task;
		core::Result<preprocess::PreparedInput> prepared;
	};

	struct InferredTask {
		Task task;
		core::Result<pipeline::InferenceOutput> output;
	};

	template<typename T>
	[[nodiscard]] static core::Result<T> cancelledResult() {
		return core::Result<T>::failure(core::Status::error(
			core::StatusCode::Cancelled, "task was cancelled"));
	}

	template<typename T>
	[[nodiscard]] static core::Result<T> stageException(
		const char* stage, const char* message) {
		return core::Result<T>::failure(core::Status::error(
			core::StatusCode::Internal,
			std::string(stage) + " stage threw an exception: " + message));
	}

	[[nodiscard]] static core::Result<TaskHandle<ResultType>> submitFailure(
		core::StatusCode code, const char* message) {
		return core::Result<TaskHandle<ResultType>>::failure(
			core::Status::error(code, message));
	}

	[[nodiscard]] bool cancelled(const Task& task) const noexcept {
		return immediateStopRequested_.load() || task.cancellationRequested();
	}

	void runPreprocess() noexcept {
		for (;;) {
			std::optional<Task> task;
			{
				std::unique_lock lock(entryMutex_);
				entryReady_.wait(lock, [this] {
					return stopRequested_ || !entryQueue_.empty();
				});
				if (stopRequested_ && entryQueue_.empty()) {
					break;
				}
				task.emplace(std::move(entryQueue_.front()));
				entryQueue_.pop_front();
			}
			entrySpaceAvailable_.notify_one();
			task->markRunning();

			auto prepared = cancelled(*task)
				? cancelledResult<preprocess::PreparedInput>()
				: callPreprocess(std::move(task->packet()));
			if (cancelled(*task)) {
				prepared = cancelledResult<preprocess::PreparedInput>();
			}
			static_cast<void>(preprocessQueue_.push(
				PreprocessedTask{std::move(*task), std::move(prepared)}));
		}
		preprocessQueue_.close();
	}

	void runInference() noexcept {
		for (;;) {
			auto task = preprocessQueue_.pop();
			if (!task) {
				break;
			}
			core::Result<pipeline::InferenceOutput> output =
				cancelled(task->task)
				? cancelledResult<pipeline::InferenceOutput>()
				: task->prepared
					? callInference(std::move(task->prepared).value())
					: core::Result<pipeline::InferenceOutput>::failure(
						task->prepared.status());
			if (cancelled(task->task)) {
				output = cancelledResult<pipeline::InferenceOutput>();
			}
			static_cast<void>(inferenceQueue_.push(
				InferredTask{std::move(task->task), std::move(output)}));
		}
		inferenceQueue_.close();
	}

	void runPostprocess() noexcept {
		for (;;) {
			auto task = inferenceQueue_.pop();
			if (!task) {
				break;
			}
			core::Result<ResultType> result = cancelled(task->task)
				? cancelledResult<ResultType>()
				: task->output
					? callPostprocess(std::move(task->output).value())
					: core::Result<ResultType>::failure(task->output.status());
			if (cancelled(task->task)) {
				result = cancelledResult<ResultType>();
			}
			pipeline_->finishExecution(task->task.executionId(), result);
			queueCompletion(std::move(task->task), std::move(result));
		}
	}

	[[nodiscard]] core::Result<preprocess::PreparedInput> callPreprocess(
		pipeline::PipelinePacket packet) noexcept {
		try {
			return pipeline_->preprocess(std::move(packet));
		} catch (const std::exception& exception) {
			return stageException<preprocess::PreparedInput>(
				"preprocess", exception.what());
		} catch (...) {
			return stageException<preprocess::PreparedInput>(
				"preprocess", "unknown exception");
		}
	}

	[[nodiscard]] core::Result<pipeline::InferenceOutput> callInference(
		preprocess::PreparedInput input) noexcept {
		try {
			return pipeline_->infer(std::move(input));
		} catch (const std::exception& exception) {
			return stageException<pipeline::InferenceOutput>(
				"inference", exception.what());
		} catch (...) {
			return stageException<pipeline::InferenceOutput>(
				"inference", "unknown exception");
		}
	}

	[[nodiscard]] core::Result<ResultType> callPostprocess(
		pipeline::InferenceOutput output) noexcept {
		try {
			return pipeline_->postprocess(std::move(output));
		} catch (const std::exception& exception) {
			return stageException<ResultType>("postprocess", exception.what());
		} catch (...) {
			return stageException<ResultType>("postprocess", "unknown exception");
		}
	}

	void queueCompletion(Task task, core::Result<ResultType> result) {
		auto sharedTask = std::make_shared<Task>(std::move(task));
		static_cast<void>(completionDispatcher_.dispatch(
			std::move(result),
			[sharedTask = std::move(sharedTask)](auto delivered) {
				sharedTask->complete(std::move(delivered));
			}));
	}

	void joinThreads() noexcept {
		std::lock_guard lock(joinMutex_);
		join(preprocessThread_);
		join(inferenceThread_);
		join(postprocessThread_);
	}

	static void join(std::thread& thread) noexcept {
		if (thread.joinable() && thread.get_id() != std::this_thread::get_id()) {
			thread.join();
		}
	}

	std::unique_ptr<pipeline::IStagedVisionPipeline<ResultType>> pipeline_;
	const std::size_t queueCapacity_;
	const QueueFullPolicy queueFullPolicy_;
	const std::size_t stageQueueCapacity_;
	std::atomic<TaskId> nextTaskId_{1};
	std::atomic_bool immediateStopRequested_{false};
	common::BoundedBlockingQueue<PreprocessedTask> preprocessQueue_;
	common::BoundedBlockingQueue<InferredTask> inferenceQueue_;
	core::CompletionDispatcher<ResultType> completionDispatcher_;

	std::mutex entryMutex_;
	std::condition_variable entryReady_;
	std::condition_variable entrySpaceAvailable_;
	std::deque<Task> entryQueue_;
	bool accepting_ = true;
	bool stopRequested_ = false;
	std::thread preprocessThread_;

	std::thread inferenceThread_;

	std::thread postprocessThread_;
	std::mutex joinMutex_;
};

} // namespace visionRuntime::executor