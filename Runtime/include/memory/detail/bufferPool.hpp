#pragma once

#include "core/result.hpp"
#include "core/tensorBuffer.hpp"

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace visionRuntime::memory::detail {

class BufferPool {
public:
	BufferPool() = default;

	template <typename Allocator>
	[[nodiscard]] static core::Result<BufferPool> create(
		std::size_t bufferCount,
		std::size_t bufferCapacity,
		std::shared_ptr<Allocator> allocator) {
		if (bufferCount == 0 || bufferCapacity == 0) {
			return invalidArgument("buffer pool dimensions must be greater than zero");
		}
		if (!allocator) {
			return invalidArgument("buffer pool allocator must not be null");
		}

		try {
			auto state = std::make_shared<State>();
			state->bufferCapacity = bufferCapacity;
			state->slots.reserve(bufferCount);
			state->available.reserve(bufferCount);
			for (std::size_t index = 0; index < bufferCount; ++index) {
				auto buffer = allocator->allocate(bufferCapacity);
				if (!buffer) {
					return core::Result<BufferPool>::failure(buffer.status());
				}
				state->slots.push_back(std::move(buffer).value());
				state->available.push_back(bufferCount - index - 1);
			}
			return core::Result<BufferPool>::success(BufferPool(std::move(state)));
		} catch (const std::bad_alloc&) {
			return core::Result<BufferPool>::failure(core::Status::error(
				core::StatusCode::ResourceExhausted, "failed to allocate buffer pool"));
		} catch (const std::length_error&) {
			return invalidArgument("buffer count exceeds the supported pool size");
		}
	}

	[[nodiscard]] core::Result<core::TensorBuffer> acquire() const {
		return acquireImpl({});
	}

	[[nodiscard]] core::Result<core::TensorBuffer> acquireBlocking(
		std::stop_token stopToken = {}) const {
		return acquireImpl(stopToken);
	}

	[[nodiscard]] std::size_t size() const noexcept {
		return state_ ? state_->slots.size() : 0;
	}

	[[nodiscard]] std::size_t bufferCapacity() const noexcept {
		return state_ ? state_->bufferCapacity : 0;
	}

	[[nodiscard]] std::size_t available() const {
		if (!state_) {
			return 0;
		}
		std::scoped_lock lock(state_->mutex);
		return state_->available.size();
	}

private:
	struct State {
		std::mutex mutex;
		std::condition_variable_any availableReady;
		std::vector<core::TensorBuffer> slots;
		std::vector<std::size_t> available;
		std::size_t bufferCapacity = 0;
	};

	explicit BufferPool(std::shared_ptr<State> state)
		: state_(std::move(state)) {}

	[[nodiscard]] core::Result<core::TensorBuffer> acquireImpl(
		std::optional<std::stop_token> stopToken) const {
		if (!state_) {
			return core::Result<core::TensorBuffer>::failure(core::Status::error(
				core::StatusCode::InvalidState, "buffer pool is not initialized"));
		}

		std::size_t index;
		{
			std::unique_lock lock(state_->mutex);
			if (stopToken && !state_->availableReady.wait(
				lock, *stopToken, [this] { return !state_->available.empty(); })) {
				return core::Result<core::TensorBuffer>::failure(core::Status::error(
					core::StatusCode::Cancelled, "buffer acquisition was cancelled"));
			}
			if (state_->available.empty()) {
				return core::Result<core::TensorBuffer>::failure(core::Status::error(
					core::StatusCode::ResourceExhausted, "buffer pool is exhausted"));
			}
			index = state_->available.back();
			state_->available.pop_back();
		}

		auto state = state_;
		auto& slot = state->slots[index];
		auto lease = std::shared_ptr<void>(slot.data(), [state, index](void*) noexcept {
			{
				std::scoped_lock lock(state->mutex);
				state->available.push_back(index);
			}
			state->availableReady.notify_one();
		});
		return core::TensorBuffer::share(
			std::move(lease), slot.data(), slot.capacity(), slot.device(),
			slot.memoryKind(), slot.isWritable());
	}

	[[nodiscard]] static core::Result<BufferPool> invalidArgument(
		std::string message) {
		return core::Result<BufferPool>::failure(core::Status::error(
			core::StatusCode::InvalidArgument, std::move(message)));
	}

	std::shared_ptr<State> state_;
};

} // namespace visionRuntime::memory::detail