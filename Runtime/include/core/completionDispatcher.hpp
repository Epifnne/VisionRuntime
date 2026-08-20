#pragma once

#include "common/boundedBlockingQueue.hpp"
#include "core/result.hpp"

#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace visionRuntime::core {

template<typename ResultType>
class CompletionDispatcher {
public:
	using Delivery = std::function<void(Result<ResultType>)>;

	explicit CompletionDispatcher(std::size_t capacity = 16)
		: queue_(capacity), worker_([this] { run(); }) {}

	~CompletionDispatcher() {
		finish();
	}

	CompletionDispatcher(const CompletionDispatcher&) = delete;
	CompletionDispatcher& operator=(const CompletionDispatcher&) = delete;
	CompletionDispatcher(CompletionDispatcher&&) = delete;
	CompletionDispatcher& operator=(CompletionDispatcher&&) = delete;

	[[nodiscard]] bool dispatch(
		Result<ResultType> result,
		Delivery delivery) {
		return queue_.push(Completion{
			std::move(result), std::move(delivery)});
	}

	void finish() noexcept {
		queue_.close();
		std::lock_guard joinLock(joinMutex_);
		if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
			worker_.join();
		}
	}

private:
	struct Completion {
		Result<ResultType> result;
		Delivery delivery;
	};

	void run() noexcept {
		for (;;) {
			auto completion = queue_.pop();
			if (!completion) {
				break;
			}
			if (completion->delivery) {
				try {
					completion->delivery(std::move(completion->result));
				} catch (...) {
				}
			}
		}
	}

	common::BoundedBlockingQueue<Completion> queue_;
	std::thread worker_;
	std::mutex joinMutex_;
};

} // namespace visionRuntime::core