#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace visionRuntime::common {
namespace detail {

inline constexpr std::size_t kCacheLineSize = 64;

struct alignas(kCacheLineSize) PaddedAtomicIndex {
	std::atomic_size_t value{0};
};

static_assert(sizeof(PaddedAtomicIndex) >= kCacheLineSize);

} // namespace detail

// Fixed-capacity SPSC ring buffer with blocking waits outside the data path.
template<typename T>
class BoundedBlockingQueue {
public:
	explicit BoundedBlockingQueue(std::size_t capacity)
		: slots_(capacity) {}

	BoundedBlockingQueue(const BoundedBlockingQueue&) = delete;
	BoundedBlockingQueue& operator=(const BoundedBlockingQueue&) = delete;
	BoundedBlockingQueue(BoundedBlockingQueue&&) = delete;
	BoundedBlockingQueue& operator=(BoundedBlockingQueue&&) = delete;

	[[nodiscard]] bool push(T value) {
		if (slots_.empty()) {
			return false;
		}
		for (;;) {
			if (tryPush(std::move(value))) {
				return true;
			}
			if (closed_.load(std::memory_order_acquire)) {
				return false;
			}
			const auto epoch = spaceEpoch_.load(std::memory_order_acquire);
			if (!closed_.load(std::memory_order_acquire) && full()) {
				spaceEpoch_.wait(epoch, std::memory_order_acquire);
			}
		}
	}

	[[nodiscard]] std::optional<T> pop() {
		for (;;) {
			if (auto value = tryPop()) {
				return value;
			}
			if (closed_.load(std::memory_order_acquire) && empty()) {
				return std::nullopt;
			}
			const auto epoch = dataEpoch_.load(std::memory_order_acquire);
			if (!(closed_.load(std::memory_order_acquire) && empty()) && empty()) {
				dataEpoch_.wait(epoch, std::memory_order_acquire);
			}
		}
	}

	void close() noexcept {
		closed_.store(true, std::memory_order_release);
		dataEpoch_.fetch_add(1, std::memory_order_release);
		spaceEpoch_.fetch_add(1, std::memory_order_release);
		dataEpoch_.notify_all();
		spaceEpoch_.notify_all();
	}

	[[nodiscard]] bool isClosed() const noexcept {
		return closed_.load();
	}

	[[nodiscard]] std::size_t capacity() const noexcept {
		return slots_.size();
	}

private:
	[[nodiscard]] bool tryPush(T&& value) {
		if (closed_.load(std::memory_order_acquire) || slots_.empty()) {
			return false;
		}
		const auto tail = tail_.value.load(std::memory_order_relaxed);
		const auto head = head_.value.load(std::memory_order_acquire);
		if (tail - head >= slots_.size()) {
			return false;
		}
		slots_[tail % slots_.size()].emplace(std::move(value));
		tail_.value.store(tail + 1, std::memory_order_release);
		dataEpoch_.fetch_add(1, std::memory_order_release);
		dataEpoch_.notify_one();
		return true;
	}

	[[nodiscard]] std::optional<T> tryPop() {
		const auto head = head_.value.load(std::memory_order_relaxed);
		const auto tail = tail_.value.load(std::memory_order_acquire);
		if (head == tail) {
			return std::nullopt;
		}
		auto& slot = slots_[head % slots_.size()];
		std::optional<T> value(std::move(*slot));
		slot.reset();
		head_.value.store(head + 1, std::memory_order_release);
		spaceEpoch_.fetch_add(1, std::memory_order_release);
		spaceEpoch_.notify_one();
		return value;
	}

	[[nodiscard]] bool empty() const noexcept {
		return head_.value.load(std::memory_order_acquire) ==
			tail_.value.load(std::memory_order_acquire);
	}

	[[nodiscard]] bool full() const noexcept {
		if (slots_.empty()) {
			return true;
		}
		return tail_.value.load(std::memory_order_acquire) -
			head_.value.load(std::memory_order_acquire) >= slots_.size();
	}

	std::vector<std::optional<T>> slots_;
	detail::PaddedAtomicIndex head_;
	detail::PaddedAtomicIndex tail_;
	std::atomic_bool closed_{false};
	std::atomic_size_t dataEpoch_{0};
	std::atomic_size_t spaceEpoch_{0};
};

} // namespace visionRuntime::common