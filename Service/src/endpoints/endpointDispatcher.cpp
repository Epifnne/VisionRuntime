#include "endpoints/endpointDispatcher.hpp"
#include "logs/logger.hpp"

namespace visionService::endpoints {

EndpointDispatcher::EndpointDispatcher(std::size_t capacity)
	: capacity_(capacity), worker_([this] { run(); }) {}

EndpointDispatcher::~EndpointDispatcher() {
	shutdown();
}

visionRuntime::core::Result<void> EndpointDispatcher::post(std::function<void()> task) {
	using visionRuntime::core::Result;
	using visionRuntime::core::Status;
	using visionRuntime::core::StatusCode;
	{
		std::lock_guard lock(mutex_);
		if (stopping_ || tasks_.size() >= capacity_) {
			auto status = Status::error(stopping_ ? StatusCode::InvalidState : StatusCode::QueueFull,
				stopping_ ? "endpoint dispatcher has stopped" : "endpoint dispatch queue is full");
			visionRuntime::logs::report(status);
			return Result<void>::failure(std::move(status));
		}
		tasks_.push_back(std::move(task));
	}
	ready_.notify_one();
	return Result<void>::success();
}

void EndpointDispatcher::drain() {
	if (isDispatcherThread()) {
		visionRuntime::logs::error("endpoint callbacks must not drain their own dispatcher");
		return;
	}
	std::unique_lock lock(mutex_);
	drained_.wait(lock, [this] { return tasks_.empty() && !active_; });
}

void EndpointDispatcher::shutdown() noexcept {
	{
		std::lock_guard lock(mutex_);
		stopping_ = true;
	}
	ready_.notify_all();
	if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
		worker_.join();
	}
}

bool EndpointDispatcher::isDispatcherThread() const noexcept {
	return std::this_thread::get_id() == worker_.get_id();
}

void EndpointDispatcher::run() noexcept {
	for (;;) {
		std::function<void()> task;
		{
			std::unique_lock lock(mutex_);
			ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
			if (tasks_.empty()) {
				return;
			}
			task = std::move(tasks_.front());
			tasks_.pop_front();
			active_ = true;
		}
		try {
			task();
		} catch (...) {
			visionRuntime::logs::error("endpoint dispatch task threw an exception");
		}
		{
			std::lock_guard lock(mutex_);
			active_ = false;
		}
		drained_.notify_all();
	}
}

} // namespace visionService::endpoints
