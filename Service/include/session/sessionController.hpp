/**
 * @file sessionController.hpp
 * @brief Session lifecycle state machine for visionService.
 *
 * Owns the control-thread contract of runtime::MultiCameraSession: start()
 * spawns the dedicated control thread, and only that thread ever calls
 * wait(). Command invocation must therefore never join the session
 * synchronously; stop is always request-stop plus asynchronous completion.
 */

#pragma once

#include "core/result.hpp"
#include "runtime/multiCameraSession.hpp"
#include "vision/anomalyResult.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace visionService {

namespace core = visionRuntime::core;
namespace runtime = visionRuntime::runtime;

namespace session {

using SessionResult = visionRuntime::vision::AnomalyResult;

enum class SessionLifecycle {
	Idle,
	Configuring,
	Running,
	Stopping
};

[[nodiscard]] constexpr std::string_view sessionLifecycleName(
	SessionLifecycle state) noexcept {
	switch (state) {
	case SessionLifecycle::Idle:
		return "idle";
	case SessionLifecycle::Configuring:
		return "configuring";
	case SessionLifecycle::Running:
		return "running";
	case SessionLifecycle::Stopping:
		return "stopping";
	}
	return "unknown";
}

struct SessionCounters {
	std::size_t received = 0;
	std::size_t submitted = 0;
	std::size_t completed = 0;
	std::size_t failed = 0;
	std::size_t dropped = 0;
	std::size_t sourceFailures = 0;
	std::vector<std::size_t> receivedPerSource;
	std::vector<std::size_t> droppedPerSource;
};

class SessionController {
public:
	using AnomalySession = runtime::MultiCameraSession<SessionResult>;

	SessionController() = default;
	~SessionController();

	SessionController(const SessionController&) = delete;
	SessionController& operator=(const SessionController&) = delete;
	SessionController(SessionController&&) = delete;
	SessionController& operator=(SessionController&&) = delete;

	[[nodiscard]] SessionLifecycle state() const;

	/// Invoked once when a run finishes (completed or stopped), with the final
	/// counters. Set before start(); called on the control thread.
	std::function<void(const SessionCounters&)> completionCallback;

	/**
	 * Adopts a fully assembled session. Blocks until any previous run has
	 * finished and replaces it. The adopted session must not be started yet.
	 */
	[[nodiscard]] core::Result<void> configure(
		std::unique_ptr<AnomalySession> session,
		std::size_t sourceCount);

	/**
	 * Starts the session on a dedicated control thread. The control thread is
	 * the only thread allowed to call wait() on the session.
	 */
	[[nodiscard]] core::Result<void> start();

	/**
	 * Non-blocking stop request; completion is observable through state() and
	 * counters(). A duration-limited session stops on its own and transitions
	 * back to Idle.
	 */
	void requestStop(
		visionRuntime::executor::StopMode mode =
			visionRuntime::executor::StopMode::Graceful) noexcept;

	[[nodiscard]] SessionCounters counters() const;
	/// Live counters while a run is in flight (read from the running session;
	/// thread-safe on MultiCameraSession). Returns nullopt when idle, so the
	/// final summary after wait() keeps flowing through counters().
	[[nodiscard]] std::optional<SessionCounters> liveCounters() const;
	[[nodiscard]] std::size_t sourceCount() const noexcept;
	[[nodiscard]] std::string lastError() const;

private:
	void controlThreadMain();
	void completionWatchMain();
	void noteError(core::Status status);
	void waitForIdle() noexcept;

	mutable std::mutex mutex_;
	SessionLifecycle state_ = SessionLifecycle::Idle;
	std::unique_ptr<AnomalySession> session_;
	/// Valid while a run is active; owned by the control thread. Set in
	/// start(), cleared by the control thread before it finishes.
	AnomalySession* runningSession_ = nullptr;
	visionRuntime::executor::StopMode pendingStopMode_ =
		visionRuntime::executor::StopMode::Graceful;
	std::size_t sourceCount_ = 0;
	SessionCounters counters_;
	std::string lastError_;
	std::thread controlThread_;
	std::thread completionWatchThread_;
	std::condition_variable wakeReady_;
};

} // namespace session
} // namespace visionService
