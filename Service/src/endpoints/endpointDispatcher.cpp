#include "endpoints/endpointDispatcher.hpp"

namespace visionService::endpoints {

EndpointDispatcher::EndpointDispatcher()
	: worker_([this] { run(); }) {}

EndpointDispatcher::~EndpointDispatcher() {
	shutdown();
}

void EndpointDispatcher::post(std::function<void()> task) {
	{
		std::lock_guard lock(mutex_);
		if (stopping_) {
			return;
		}
		tasks_.push_back(std::move(task));
	}
	ready_.notify_one();
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
		}
		try {
			task();
		} catch (...) {
		}
	}
}

} // namespace visionService::endpoints
