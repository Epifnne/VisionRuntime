#pragma once

#include "executor/executorTask.hpp"
#include "executor/iPipelineExecutor.hpp"
#include "logs/logger.hpp"
#include "pipeline/iVisionPipeline.hpp"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace visionRuntime::executor {

struct TaskParallelOptions {
	std::size_t workerCount = 1;
	std::size_t maxInFlight = 16;
};

template<typename ResultType>
using PipelineFactory = std::function<
	core::Result<std::unique_ptr<pipeline::IVisionPipeline<ResultType>>>()>;

template<typename ResultType>
class TaskParallelPipelineExecutor final : public IPipelineExecutor<ResultType> {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<TaskParallelPipelineExecutor>> create(
		const PipelineFactory<ResultType>& factory,
		ExecutorOptions options = {},
		TaskParallelOptions parallel = {}) {
		if (!factory || options.queueCapacity == 0 || parallel.workerCount == 0 ||
			parallel.maxInFlight < parallel.workerCount ||
			(options.taskTimeout &&
				(options.taskTimeout->count() <= 0 ||
				 options.taskTimeout->count() > 86400000))) {
			return failure<std::unique_ptr<TaskParallelPipelineExecutor>>(
				core::StatusCode::InvalidArgument,
				"task parallel execution requires a factory, positive capacities, "
				"maxInFlight >= workerCount and a valid task timeout");
		}
		try {
			std::vector<std::unique_ptr<pipeline::IVisionPipeline<ResultType>>> pipelines;
			pipelines.reserve(parallel.workerCount);
			for (std::size_t i = 0; i < parallel.workerCount; ++i) {
				auto built = factory();
				if (!built) {
					logs::report(built.status());
					return core::Result<std::unique_ptr<TaskParallelPipelineExecutor>>::failure(
						built.status());
				}
				if (!built.value()) {
					return failure<std::unique_ptr<TaskParallelPipelineExecutor>>(
						core::StatusCode::InvalidArgument, "pipeline factory returned null");
				}
				pipelines.push_back(std::move(built).value());
			}
			return core::Result<std::unique_ptr<TaskParallelPipelineExecutor>>::success(
				std::unique_ptr<TaskParallelPipelineExecutor>(
					new TaskParallelPipelineExecutor(std::move(pipelines), options, parallel)));
		} catch (const std::exception& error) {
			return failure<std::unique_ptr<TaskParallelPipelineExecutor>>(
				core::StatusCode::Internal,
				std::string("task parallel executor construction failed: ") + error.what());
		} catch (...) {
			return failure<std::unique_ptr<TaskParallelPipelineExecutor>>(
				core::StatusCode::Internal, "task parallel executor construction failed");
		}
	}

	~TaskParallelPipelineExecutor() override {
		requestStop(StopMode::Immediate);
		wait();
	}

	TaskParallelPipelineExecutor(const TaskParallelPipelineExecutor&) = delete;
	TaskParallelPipelineExecutor& operator=(const TaskParallelPipelineExecutor&) = delete;

	[[nodiscard]] core::Result<TaskHandle<ResultType>> submit(
		pipeline::PipelinePacket packet,
		CompletionCallback<ResultType> callback = {}) override {
		const auto deadline = taskDeadline(packet.identity(), options_.taskTimeout);
		std::unique_lock lock(mutex_);
		if (!accepting_) {
			return failure<TaskHandle<ResultType>>(core::StatusCode::InvalidState,
				"executor has stopped accepting tasks");
		}
		const auto space = [this] {
			return pending_.size() < options_.queueCapacity && inFlight_ < slots_.size();
		};
		if (options_.queueFullPolicy == QueueFullPolicy::Block && !space()) {
			if (deliveryThread_.get_id() == std::this_thread::get_id()) {
				return failure<TaskHandle<ResultType>>(core::StatusCode::InvalidState,
					"completion callback cannot block on its own executor capacity");
			}
			if (!waitForEntry(spaceReady_, lock, deadline,
					[&] { return !accepting_ || space(); })) {
				return failure<TaskHandle<ResultType>>(core::StatusCode::DeadlineExceeded,
					"task deadline expired while waiting for executor capacity");
			}
			if (!accepting_) {
				return failure<TaskHandle<ResultType>>(core::StatusCode::InvalidState,
					"executor has stopped accepting tasks");
			}
		}
		if (deadlineExceeded(deadline)) {
			return failure<TaskHandle<ResultType>>(core::StatusCode::DeadlineExceeded,
				"task deadline expired before submission");
		}
		if (!space()) {
			return failure<TaskHandle<ResultType>>(core::StatusCode::QueueFull,
				"executor queue or maxInFlight reached its capacity");
		}
		const auto index = nextAdmission_;
		Task task(nextTaskId_, std::move(packet), std::move(callback), deadline);
		auto handle = task.handle();
		pending_.push_back(index);
		slots_[index].task.emplace(std::move(task));
		nextAdmission_ = (index + 1) % slots_.size();
		++nextTaskId_;
		++inFlight_;
		workReady_.notify_one();
		return core::Result<TaskHandle<ResultType>>::success(std::move(handle));
	}

	void requestStop(StopMode mode = StopMode::Graceful) noexcept override {
		{
			std::lock_guard lock(mutex_);
			accepting_ = false;
			if (mode == StopMode::Immediate) {
				for (auto& slot : slots_) {
					if (slot.task) slot.task->requestCancellation();
				}
				if (delivering_) delivering_->requestCancellation();
			}
		}
		workReady_.notify_all();
		completionReady_.notify_all();
		spaceReady_.notify_all();
	}

	void wait() noexcept override {
		requestStop();
		std::lock_guard lock(joinMutex_);
		if (finished_) return;
		for (auto& worker : workers_) {
			if (worker.joinable()) worker.join();
		}
		if (deliveryThread_.joinable()) deliveryThread_.join();
		for (auto& pipeline : pipelines_) pipeline->finishBatch();
		finished_ = true;
	}

private:
	using Task = ExecutorTask<ResultType>;
	struct Slot {
		std::optional<Task> task;
		std::optional<core::Result<ResultType>> result;
	};

	TaskParallelPipelineExecutor(
		std::vector<std::unique_ptr<pipeline::IVisionPipeline<ResultType>>> pipelines,
		ExecutorOptions options, TaskParallelOptions parallel)
		: options_(options), pipelines_(std::move(pipelines)), slots_(parallel.maxInFlight) {
		try {
			workers_.reserve(pipelines_.size());
			for (std::size_t i = 0; i < pipelines_.size(); ++i) {
				workers_.emplace_back([this, i] { runWorker(*pipelines_[i]); });
			}
			deliveryThread_ = std::thread([this] { runDelivery(); });
		} catch (...) {
			requestStop(StopMode::Immediate);
			for (auto& worker : workers_) if (worker.joinable()) worker.join();
			throw;
		}
	}

	template<typename T>
	[[nodiscard]] static core::Result<T> failure(core::StatusCode code, std::string message) {
		auto status = core::Status::error(code, std::move(message));
		logs::report(status);
		return core::Result<T>::failure(std::move(status));
	}

	[[nodiscard]] static core::Result<ResultType> execute(
		pipeline::IVisionPipeline<ResultType>& pipeline, Task& task) noexcept {
		if (task.cancellationRequested()) {
			return core::Result<ResultType>::failure(core::Status::error(
				core::StatusCode::Cancelled, "task was cancelled"));
		}
		if (task.expired()) {
			return core::Result<ResultType>::failure(core::Status::error(
				core::StatusCode::DeadlineExceeded, "task deadline expired before execution"));
		}
		task.markRunning();
		try {
			return pipeline.run(std::move(task.packet()));
		} catch (const std::exception& error) {
			return failure<ResultType>(core::StatusCode::Internal,
				std::string("pipeline execution threw an exception: ") + error.what());
		} catch (...) {
			return failure<ResultType>(core::StatusCode::Internal,
				"pipeline execution threw an unknown exception");
		}
	}

	void runWorker(pipeline::IVisionPipeline<ResultType>& pipeline) noexcept {
		for (;;) {
			std::size_t index;
			{
				std::unique_lock lock(mutex_);
				workReady_.wait(lock, [this] { return !accepting_ || !pending_.empty(); });
				if (pending_.empty()) return;
				index = pending_.front();
				pending_.pop_front();
			}
			spaceReady_.notify_all();
			auto result = execute(pipeline, *slots_[index].task);
			{
				std::lock_guard lock(mutex_);
				slots_[index].result.emplace(std::move(result));
			}
			completionReady_.notify_one();
		}
	}

	void runDelivery() noexcept {
		std::size_t next = 0;
		for (;;) {
			std::unique_lock lock(mutex_);
			completionReady_.wait(lock, [&] {
				return (!accepting_ && inFlight_ == 0) || slots_[next].result.has_value();
			});
			if (inFlight_ == 0) return;
			auto task = std::move(*slots_[next].task);
			auto result = std::move(*slots_[next].result);
			slots_[next].task.reset();
			slots_[next].result.reset();
			delivering_ = &task;
			lock.unlock();
			if (task.cancellationRequested()) {
				result = core::Result<ResultType>::failure(core::Status::error(
					core::StatusCode::Cancelled, "task was cancelled"));
			}
			task.complete(std::move(result));
			lock.lock();
			delivering_ = nullptr;
			--inFlight_;
			next = (next + 1) % slots_.size();
			lock.unlock();
			spaceReady_.notify_all();
		}
	}

	const ExecutorOptions options_;
	std::vector<std::unique_ptr<pipeline::IVisionPipeline<ResultType>>> pipelines_;
	// Slots stay reserved through callback completion, bounding out-of-order results too.
	std::vector<Slot> slots_;
	std::deque<std::size_t> pending_;
	std::mutex mutex_;
	std::condition_variable workReady_, completionReady_, spaceReady_;
	std::size_t nextAdmission_ = 0;
	TaskId nextTaskId_ = 1;
	std::size_t inFlight_ = 0;
	Task* delivering_ = nullptr;
	bool accepting_ = true;
	std::vector<std::thread> workers_;
	std::thread deliveryThread_;
	std::mutex joinMutex_;
	bool finished_ = false;
};

} // namespace visionRuntime::executor
