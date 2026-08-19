#pragma once

#include "core/result.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <utility>

namespace visionRuntime::executor {

using TaskId = std::uint64_t;

enum class TaskState {
	Queued,
	Running,
	Completed,
	Failed,
	Cancelled
};

namespace detail {

template<typename ResultType>
struct TaskSharedState {
	TaskSharedState()
		: future(promise.get_future().share()) {}

	std::promise<core::Result<ResultType>> promise;
	std::shared_future<core::Result<ResultType>> future;
	std::atomic<TaskState> state{TaskState::Queued};
	std::atomic<bool> cancelRequested{false};
};

} // namespace detail

template<typename ResultType>
class TaskHandle {
public:
	TaskHandle() = default;

	[[nodiscard]] TaskId id() const noexcept {
		return id_;
	}

	[[nodiscard]] TaskState state() const noexcept {
		return state_ ? state_->state.load() : TaskState::Cancelled;
	}

	[[nodiscard]] bool cancel() noexcept {
		if (!state_) {
			return false;
		}

		auto current = state_->state.load();
		while (current == TaskState::Queued || current == TaskState::Running) {
			if (state_->cancelRequested.exchange(true)) {
				return false;
			}
			return true;
		}
		return false;
	}

	[[nodiscard]] const std::shared_future<core::Result<ResultType>>& future() const noexcept {
		return state_->future;
	}

private:
	template<typename>
	friend class PipelineExecutor;

	TaskHandle(TaskId id, std::shared_ptr<detail::TaskSharedState<ResultType>> state)
		: id_(id), state_(std::move(state)) {}

	TaskId id_ = 0;
	std::shared_ptr<detail::TaskSharedState<ResultType>> state_;
};

template<typename ResultType>
using CompletionCallback = std::function<void(
	TaskId, const core::Result<ResultType>&)>;

} // namespace visionRuntime::executor