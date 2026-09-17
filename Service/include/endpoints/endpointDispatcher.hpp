/**
 * @file endpointDispatcher.hpp
 * @brief Serial delivery of endpoint subscription callbacks on a dedicated thread.
 */

#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace visionService::endpoints {

/**
 * Owns the single service dispatch thread. All subscription callbacks
 * (state snapshots and stream frames) are posted here and delivered
 * serially, so subscribers never run on camera or pipeline threads.
 *
 * shutdown() is idempotent and drains already queued tasks; the destructor
 * calls it. Owners must call shutdown() before destroying the registries
 * whose tasks reference them.
 */
class EndpointDispatcher {
public:
	EndpointDispatcher();
	~EndpointDispatcher();

	EndpointDispatcher(const EndpointDispatcher&) = delete;
	EndpointDispatcher& operator=(const EndpointDispatcher&) = delete;
	EndpointDispatcher(EndpointDispatcher&&) = delete;
	EndpointDispatcher& operator=(EndpointDispatcher&&) = delete;

	void post(std::function<void()> task);
	void shutdown() noexcept;
	[[nodiscard]] bool isDispatcherThread() const noexcept;

private:
	void run() noexcept;

	mutable std::mutex mutex_;
	std::condition_variable ready_;
	std::deque<std::function<void()>> tasks_;
	bool stopping_ = false;
	std::thread worker_;
};

} // namespace visionService::endpoints
