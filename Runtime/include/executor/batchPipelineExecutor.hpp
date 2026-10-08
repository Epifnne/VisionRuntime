#pragma once

#include "common/boundedBlockingQueue.hpp"
#include "core/completionDispatcher.hpp"
#include "core/result.hpp"
#include "executor/executorOptions.hpp"
#include "executor/executorTask.hpp"
#include "executor/iPipelineExecutor.hpp"
#include "pipeline/iStagedVisionPipeline.hpp"
#include "preprocess/preparedInput.hpp"

#include <atomic>
#include <chrono>
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
#include <vector>

namespace visionRuntime::executor {

struct BatchInferenceOptions {
	std::size_t maxBatchSize = 1;
	std::chrono::milliseconds flushTimeout{10};
};

/**
 * Aggregates preprocessed frames from multiple sources into batches and runs
 * one pipeline inference per batch via IStagedVisionPipeline::inferBatch.
 * Batch member i always maps to the i-th submitted task of that batch (FIFO),
 * so per-task futures and callbacks keep their single-frame semantics.
 */
template<typename ResultType>
class BatchPipelineExecutor final : public IPipelineExecutor<ResultType> {
public:
	BatchPipelineExecutor(
		std::unique_ptr<pipeline::IStagedVisionPipeline<ResultType>> pipeline,
		ExecutorOptions options,
		BatchInferenceOptions batchOptions)
		: pipeline_(std::move(pipeline)),
		  queueCapacity_(options.queueCapacity),
		  queueFullPolicy_(options.queueFullPolicy),
		  taskTimeout_(options.taskTimeout),
		  stageQueueCapacity_(options.stageQueueCapacity),
		  maxBatchSize_(batchOptions.maxBatchSize == 0 ? 1 : batchOptions.maxBatchSize),
		  flushTimeout_(batchOptions.flushTimeout),
		  preprocessQueue_(options.stageQueueCapacity),
		  postprocessQueue_(options.stageQueueCapacity),
		  completionDispatcher_(options.stageQueueCapacity) {
		preprocessThread_ = std::thread([this] { runPreprocess(); });
		batchInferenceThread_ = std::thread([this] { runBatchInference(); });
		postprocessThread_ = std::thread([this] { runPostprocess(); });
	}

	~BatchPipelineExecutor() override {
		requestStop(StopMode::Immediate);
		wait();
	}

	BatchPipelineExecutor(const BatchPipelineExecutor&) = delete;
	BatchPipelineExecutor& operator=(const BatchPipelineExecutor&) = delete;
	BatchPipelineExecutor(BatchPipelineExecutor&&) = delete;
	BatchPipelineExecutor& operator=(BatchPipelineExecutor&&) = delete;

	[[nodiscard]] core::Result<TaskHandle<ResultType>> submit(
		pipeline::PipelinePacket packet,
		CompletionCallback<ResultType> callback = {}) override {
		std::unique_lock lock(entryMutex_);
		if (!pipeline_) {
			return submitFailure(core::StatusCode::InvalidState,
				"batch executor requires a staged pipeline");
		}
		if (queueCapacity_ == 0 || stageQueueCapacity_ == 0) {
			return submitFailure(core::StatusCode::InvalidArgument,
				"executor queue capacities must be greater than zero");
		}
		if (taskTimeout_ &&
			(taskTimeout_->count() <= 0 || taskTimeout_->count() > 86400000)) {
			return submitFailure(core::StatusCode::InvalidArgument,
				"task timeout must be in [1, 86400000] milliseconds");
		}
		const auto deadline = taskDeadline(packet.identity(), taskTimeout_);
		if (!accepting_) {
			return submitFailure(core::StatusCode::InvalidState,
				"executor has stopped accepting tasks");
		}
		if (queueFullPolicy_ == QueueFullPolicy::Block) {
			if (!waitForEntry(entrySpaceAvailable_, lock, deadline, [this] {
				return !accepting_ || entryQueue_.size() < queueCapacity_;
			})) {
				return submitFailure(core::StatusCode::DeadlineExceeded,
					"task deadline expired while waiting for queue space");
			}
			if (!accepting_) {
				return submitFailure(core::StatusCode::InvalidState,
					"executor has stopped accepting tasks");
			}
		}
		if (deadlineExceeded(deadline)) {
			return submitFailure(core::StatusCode::DeadlineExceeded,
				"task deadline expired before submission");
		}
		if (entryQueue_.size() >= queueCapacity_) {
			return submitFailure(core::StatusCode::QueueFull,
				"executor queue reached its capacity");
		}

		const auto taskId = nextTaskId_.fetch_add(1);
		Task task(taskId, std::move(packet), std::move(callback), deadline);
		auto handle = task.handle();
		entryQueue_.push_back(std::move(task));
		entryReady_.notify_one();
		return core::Result<TaskHandle<ResultType>>::success(
			std::move(handle));
	}

	void requestStop(StopMode mode = StopMode::Graceful) noexcept override {
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
	}

	void wait() noexcept override {
		requestStop();
		joinThreads();
		completionDispatcher_.closeInput();
		completionDispatcher_.wait();
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

	struct BatchMember {
		Task task;
		preprocess::PreparedInput prepared;
	};

	struct CompletedTask {
		Task task;
		core::Result<ResultType> result;
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
				: task->expired()
					? core::Result<preprocess::PreparedInput>::failure(
						core::Status::error(core::StatusCode::DeadlineExceeded,
							"task deadline expired before preprocessing"))
					: callPreprocess(std::move(task->packet()));
			if (cancelled(*task)) {
				prepared = cancelledResult<preprocess::PreparedInput>();
			}
			static_cast<void>(preprocessQueue_.push(
				PreprocessedTask{std::move(*task), std::move(prepared)}));
		}
		preprocessQueue_.close();
	}

	void runBatchInference() noexcept {
		for (;;) {
			auto first = preprocessQueue_.pop();
			if (!first) {
				break;
			}
			if (cancelled(first->task)) {
				stageCompletion(std::move(first->task),
					cancelledResult<ResultType>());
				continue;
			}
			if (!first->prepared) {
				stageCompletion(std::move(first->task),
					core::Result<ResultType>::failure(first->prepared.status()));
				continue;
			}

			std::vector<BatchMember> members;
			members.reserve(maxBatchSize_);
			members.push_back(BatchMember{
				std::move(first->task), std::move(first->prepared).value()});
			if (!collectBatch(members)) {
				// preprocessQueue_ closed and drained: flush what we have, then exit.
				flushBatch(std::move(members));
				break;
			}
			flushBatch(std::move(members));
		}
		postprocessQueue_.close();
	}

	// Returns false when the upstream queue closed and has been fully drained.
	[[nodiscard]] bool collectBatch(std::vector<BatchMember>& members) noexcept {
		const auto deadline = std::chrono::steady_clock::now() + flushTimeout_;
		while (members.size() < maxBatchSize_) {
			const auto remaining = std::chrono::duration_cast<
				std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
			if (remaining.count() <= 0) {
				return true;
			}
			auto next = preprocessQueue_.popFor(remaining);
			if (!next) {
				return !preprocessQueue_.isClosed();
			}
			if (cancelled(next->task)) {
				flushBatch(std::move(members));
				members.clear();
				stageCompletion(std::move(next->task),
					cancelledResult<ResultType>());
				continue;
			}
			if (!next->prepared) {
				flushBatch(std::move(members));
				members.clear();
				stageCompletion(std::move(next->task),
					core::Result<ResultType>::failure(next->prepared.status()));
				continue;
			}
			members.push_back(BatchMember{
				std::move(next->task), std::move(next->prepared).value()});
		}
		return true;
	}

	void flushBatch(std::vector<BatchMember> members) noexcept {
		if (members.empty()) {
			return;
		}
		std::vector<preprocess::PreparedInput> inputs;
		std::vector<std::size_t> active;
		std::vector<std::optional<core::Result<ResultType>>> results(members.size());
		inputs.reserve(members.size());
		for (std::size_t index = 0; index < members.size(); ++index) {
			auto& member = members[index];
			if (cancelled(member.task)) {
				results[index] = cancelledResult<ResultType>();
			} else if (member.task.expired()) {
				results[index] = core::Result<ResultType>::failure(core::Status::error(
					core::StatusCode::DeadlineExceeded,
					"task deadline expired while waiting for its inference batch"));
			} else {
				active.push_back(index);
				inputs.push_back(std::move(member.prepared));
			}
		}
		if (!active.empty()) {
			auto outputs = callInferBatch(std::move(inputs));
			if (!outputs || outputs->size() != active.size()) {
				const auto status = !outputs ? outputs.status() :
					core::Status::error(core::StatusCode::ShapeMismatch,
						"batch inference must return one output per submitted member");
				for (const auto index : active) {
					results[index] = core::Result<ResultType>::failure(status);
				}
			} else {
				for (std::size_t position = 0; position < active.size(); ++position) {
					const auto index = active[position];
					if (cancelled(members[index].task)) {
						results[index] = cancelledResult<ResultType>();
					} else if (members[index].task.expired()) {
						results[index] = core::Result<ResultType>::failure(core::Status::error(
							core::StatusCode::DeadlineExceeded,
							"task deadline expired during batch inference"));
					} else {
						results[index] = callPostprocess(std::move(outputs.value()[position]));
					}
				}
			}
		}
		for (std::size_t index = 0; index < members.size(); ++index) {
			stageCompletion(std::move(members[index].task), std::move(*results[index]));
		}
	}

	void runPostprocess() noexcept {
		for (;;) {
			auto task = postprocessQueue_.pop();
			if (!task) {
				break;
			}
			queueCompletion(std::move(task->task), std::move(task->result));
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

	[[nodiscard]] core::Result<std::vector<pipeline::InferenceOutput>>
	callInferBatch(std::vector<preprocess::PreparedInput> inputs) noexcept {
		try {
			return pipeline_->inferBatch(std::move(inputs));
		} catch (const std::exception& exception) {
			return stageException<std::vector<pipeline::InferenceOutput>>(
				"inference", exception.what());
		} catch (...) {
			return stageException<std::vector<pipeline::InferenceOutput>>(
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

	void stageCompletion(Task task, core::Result<ResultType> result) {
		static_cast<void>(postprocessQueue_.push(
			CompletedTask{std::move(task), std::move(result)}));
	}

	void queueCompletion(Task task, core::Result<ResultType> result) {
		if (cancelled(task)) {
			result = cancelledResult<ResultType>();
		} else if (result && task.expired()) {
			result = core::Result<ResultType>::failure(core::Status::error(
				core::StatusCode::DeadlineExceeded,
				"task deadline expired before result delivery"));
		}
		pipeline_->finishExecution(task.executionId(), result);
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
		join(batchInferenceThread_);
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
	const std::optional<std::chrono::milliseconds> taskTimeout_;
	const std::size_t stageQueueCapacity_;
	const std::size_t maxBatchSize_;
	const std::chrono::milliseconds flushTimeout_;
	std::atomic<TaskId> nextTaskId_{1};
	std::atomic_bool immediateStopRequested_{false};
	common::BoundedBlockingQueue<PreprocessedTask> preprocessQueue_;
	common::BoundedBlockingQueue<CompletedTask> postprocessQueue_;
	core::CompletionDispatcher<ResultType> completionDispatcher_;

	std::mutex entryMutex_;
	std::condition_variable entryReady_;
	std::condition_variable entrySpaceAvailable_;
	std::deque<Task> entryQueue_;
	bool accepting_ = true;
	bool stopRequested_ = false;
	std::thread preprocessThread_;

	std::thread batchInferenceThread_;

	std::thread postprocessThread_;
	std::mutex joinMutex_;
};

} // namespace visionRuntime::executor
