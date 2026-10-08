#pragma once

#include "executor/executorOptions.hpp"
#include "pipeline/frameIdentity.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace visionRuntime::executor {

using TaskDeadline = std::optional<std::chrono::steady_clock::time_point>;

[[nodiscard]] inline TaskDeadline taskDeadline(
	const pipeline::FrameIdentity& identity,
	const std::optional<std::chrono::milliseconds>& timeout) {
	if (!timeout) {
		return std::nullopt;
	}
	const auto captured = identity.metadata.capturedAt;
	const auto origin = captured == std::chrono::steady_clock::time_point{}
		? std::chrono::steady_clock::now() : captured;
	return origin + *timeout;
}

[[nodiscard]] inline bool deadlineExceeded(TaskDeadline deadline) noexcept {
	return deadline && std::chrono::steady_clock::now() >= *deadline;
}

template<typename Predicate>
[[nodiscard]] bool waitForEntry(
	std::condition_variable& ready,
	std::unique_lock<std::mutex>& lock,
	TaskDeadline deadline,
	Predicate predicate) {
	if (deadline) {
		return ready.wait_until(lock, *deadline, predicate);
	}
	ready.wait(lock, predicate);
	return true;
}

} // namespace visionRuntime::executor
