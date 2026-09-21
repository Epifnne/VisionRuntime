#include "session/sessionController.hpp"

#include <utility>

namespace visionService::session {

namespace {

core::Result<void> failure(core::StatusCode code, std::string message) {
	return core::Result<void>::failure(
		core::Status::error(code, std::move(message)));
}

} // namespace

SessionController::~SessionController() {
	requestStop(visionRuntime::executor::StopMode::Immediate);
	waitForIdle();
}

SessionLifecycle SessionController::state() const {
	std::lock_guard lock(mutex_);
	return state_;
}

core::Result<void> SessionController::configure(
	std::unique_ptr<AnomalySession> session,
	std::size_t sourceCount) {
	if (!session) {
		return failure(core::StatusCode::InvalidArgument,
			"session controller requires an assembled session");
	}
	if (sourceCount == 0) {
		return failure(core::StatusCode::InvalidArgument,
			"session controller requires at least one source");
	}
	waitForIdle();
	std::lock_guard lock(mutex_);
	session_ = std::move(session);
	sourceCount_ = sourceCount;
	counters_ = {};
	counters_.receivedPerSource.assign(sourceCount_, 0);
	counters_.droppedPerSource.assign(sourceCount_, 0);
	state_ = SessionLifecycle::Idle;
	return core::Result<void>::success();
}

core::Result<void> SessionController::start() {
	{
		std::lock_guard lock(mutex_);
		if (state_ == SessionLifecycle::Running) {
			return failure(core::StatusCode::InvalidState,
				"session is already running");
		}
		if (state_ == SessionLifecycle::Stopping) {
			return failure(core::StatusCode::InvalidState,
				"session is stopping; wait for it to reach idle");
		}
		if (!session_) {
			return failure(core::StatusCode::InvalidState,
				"session has not been configured");
		}
		state_ = SessionLifecycle::Running;
		runningSession_ = session_.get();
		counters_ = {};
		counters_.receivedPerSource.assign(sourceCount_, 0);
		counters_.droppedPerSource.assign(sourceCount_, 0);
		pendingStopMode_ = visionRuntime::executor::StopMode::Graceful;
	}
	// The control thread is the only thread allowed to call wait().
	controlThread_ = std::thread([this] { controlThreadMain(); });
	// Finite sources finish on their own; a watcher notices and triggers the
	// graceful stop so the control thread collects the summary.
	completionWatchThread_ = std::thread([this] { completionWatchMain(); });
	return core::Result<void>::success();
}

void SessionController::requestStop(
	visionRuntime::executor::StopMode mode) noexcept {
	std::lock_guard lock(mutex_);
	if (state_ != SessionLifecycle::Running &&
		state_ != SessionLifecycle::Stopping) {
		return;
	}
	state_ = SessionLifecycle::Stopping;
	pendingStopMode_ = mode;
	wakeReady_.notify_all();
}

SessionCounters SessionController::counters() const {
	std::lock_guard lock(mutex_);
	return counters_;
}

std::optional<SessionCounters> SessionController::liveCounters() const {
	AnomalySession* session = nullptr;
	{
		std::lock_guard lock(mutex_);
		if (state_ != SessionLifecycle::Running &&
			state_ != SessionLifecycle::Stopping) {
			return std::nullopt;
		}
		session = runningSession_;
	}
	if (session == nullptr) {
		return std::nullopt;
	}
	const auto summary = session->currentSummary();
	SessionCounters counters;
	counters.received = summary.received;
	counters.submitted = summary.submitted;
	counters.completed = summary.completed;
	counters.failed = summary.failed;
	counters.dropped = summary.dropped;
	counters.sourceFailures = summary.sourceFailures;
	counters.receivedPerSource = summary.receivedPerSource;
	counters.droppedPerSource = summary.droppedPerSource;
	return counters;
}

std::size_t SessionController::sourceCount() const noexcept {
	return sourceCount_;
}

std::string SessionController::lastError() const {
	std::lock_guard lock(mutex_);
	return lastError_;
}

void SessionController::noteError(core::Status status) {
	std::lock_guard lock(mutex_);
	if (lastError_.empty()) {
		lastError_ = status.toString();
	}
}

void SessionController::waitForIdle() noexcept {
	if (completionWatchThread_.joinable()) {
		if (completionWatchThread_.get_id() == std::this_thread::get_id()) {
			return;
		}
		completionWatchThread_.join();
	}
	if (controlThread_.joinable()) {
		if (controlThread_.get_id() == std::this_thread::get_id()) {
			return;
		}
		controlThread_.join();
	}
}

void SessionController::completionWatchMain() {
	for (;;) {
		AnomalySession* session = nullptr;
		{
			std::lock_guard lock(mutex_);
			if (state_ != SessionLifecycle::Running) {
				return;
			}
			session = runningSession_;
		}
		if (session == nullptr) {
			return;
		}
		bool anyRunning = false;
		for (const auto& source : session->sources()) {
			if (source && source->isRunning()) {
				anyRunning = true;
				break;
			}
		}
		if (!anyRunning) {
			requestStop();
			return;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

void SessionController::controlThreadMain() {
	// Take ownership of the session for the whole run; requestStop() only
	// signals through pendingStopMode_ and never touches the session.
	std::unique_ptr<AnomalySession> session;
	{
		std::lock_guard lock(mutex_);
		session = std::move(session_);
	}
	if (!session) {
		std::lock_guard lock(mutex_);
		runningSession_ = nullptr;
		state_ = SessionLifecycle::Idle;
		return;
	}
	auto started = session->start();
	if (!started) {
		noteError(started.status());
		std::lock_guard lock(mutex_);
		session_ = std::move(session);
		runningSession_ = nullptr;
		state_ = SessionLifecycle::Idle;
		return;
	}
	for (;;) {
		std::unique_lock lock(mutex_);
		wakeReady_.wait_for(lock, std::chrono::milliseconds(200), [this] {
			return state_ == SessionLifecycle::Stopping;
		});
		if (state_ == SessionLifecycle::Stopping) {
			session->requestStop(pendingStopMode_);
			break;
		}
	}
	// Only this control thread calls wait().
	const auto summary = session->wait();
	{
		std::lock_guard lock(mutex_);
		counters_.received = summary.received;
		counters_.submitted = summary.submitted;
		counters_.completed = summary.completed;
		counters_.failed = summary.failed;
		counters_.dropped = summary.dropped;
		counters_.sourceFailures = summary.sourceFailures;
		counters_.receivedPerSource = summary.receivedPerSource;
		counters_.droppedPerSource = summary.droppedPerSource;
		session_ = std::move(session);
		runningSession_ = nullptr;
		state_ = SessionLifecycle::Idle;
	}
	if (completionCallback) {
		completionCallback(counters());
	}
}

} // namespace visionService::session
