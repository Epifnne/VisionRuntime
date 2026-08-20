#pragma once

#include "core/result.hpp"
#include "executor/taskHandle.hpp"
#include "pipeline/pipelinePacket.hpp"

#include <cstdint>
#include <memory>
#include <utility>

namespace visionRuntime::executor {

template<typename ResultType>
class ExecutorTask {
public:
	ExecutorTask(
		TaskId id,
		pipeline::PipelinePacket packet,
		CompletionCallback<ResultType> callback = {})
		: id_(id),
		  executionId_(packet.executionId()),
		  packet_(std::move(packet)),
		  callback_(std::move(callback)),
		  state_(std::make_shared<detail::TaskSharedState<ResultType>>()) {}

	ExecutorTask(const ExecutorTask&) = delete;
	ExecutorTask& operator=(const ExecutorTask&) = delete;
	ExecutorTask(ExecutorTask&&) noexcept = default;
	ExecutorTask& operator=(ExecutorTask&&) noexcept = default;

	[[nodiscard]] TaskId id() const noexcept { return id_; }
	[[nodiscard]] std::uint64_t executionId() const noexcept { return executionId_; }
	[[nodiscard]] pipeline::PipelinePacket& packet() noexcept { return packet_; }
	[[nodiscard]] TaskHandle<ResultType> handle() const {
		return TaskHandle<ResultType>(id_, state_);
	}
	[[nodiscard]] bool cancellationRequested() const noexcept {
		return state_->cancelRequested.load();
	}

	void markRunning() noexcept {
		state_->state.store(TaskState::Running);
	}

	void requestCancellation() noexcept {
		state_->cancelRequested.store(true);
	}

	void complete(core::Result<ResultType> result) noexcept {
		if (result) {
			state_->state.store(TaskState::Completed);
		} else if (result.status().code() == core::StatusCode::Cancelled) {
			state_->state.store(TaskState::Cancelled);
		} else {
			state_->state.store(TaskState::Failed);
		}
		state_->promise.set_value(std::move(result));
		if (callback_) {
			try {
				callback_(id_, state_->future.get());
			} catch (...) {
			}
		}
	}

private:
	TaskId id_;
	std::uint64_t executionId_;
	pipeline::PipelinePacket packet_;
	CompletionCallback<ResultType> callback_;
	std::shared_ptr<detail::TaskSharedState<ResultType>> state_;
};

} // namespace visionRuntime::executor