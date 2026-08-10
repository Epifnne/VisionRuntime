/**
 * @file tensorBufferPool.hpp
 * @author epifnne
 * @date 2026-08-10
 * @brief Defines a thread-safe fixed-capacity pool of reusable tensor buffers.
 */

#pragma once

#include "core/result.hpp"
#include "core/tensorBuffer.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace visonRuntime::core {

class TensorBufferPool {
public:
	TensorBufferPool() = default;

	[[nodiscard]] static Result<TensorBufferPool> create(
		std::size_t bufferCount, std::size_t bufferCapacity) {
		if (bufferCount == 0 || bufferCapacity == 0) {
			return invalidArgument("buffer pool dimensions must be greater than zero");
		}

		try {
			auto state = std::make_shared<State>();
			state->bufferCapacity = bufferCapacity;
			state->slots.reserve(bufferCount);
			state->available.reserve(bufferCount);
			for (std::size_t index = 0; index < bufferCount; ++index) {
				state->slots.push_back(std::make_unique<std::byte[]>(bufferCapacity));
				state->available.push_back(bufferCount - index - 1);
			}
			return Result<TensorBufferPool>::success(TensorBufferPool(std::move(state)));
		} catch (const std::bad_alloc&) {
			return Result<TensorBufferPool>::failure(Status::error(
				StatusCode::ResourceExhausted, "failed to allocate buffer pool"));
		} catch (const std::length_error&) {
			return invalidArgument("buffer count exceeds the supported pool size");
		}
	}

	[[nodiscard]] Result<TensorBuffer> acquire() const {
		if (!state_) {
			return Result<TensorBuffer>::failure(Status::error(
				StatusCode::InvalidState, "buffer pool is not initialized"));
		}

		std::size_t index;
		void* data;
		{
			std::scoped_lock lock(state_->mutex);
			if (state_->available.empty()) {
				return Result<TensorBuffer>::failure(Status::error(
					StatusCode::ResourceExhausted, "buffer pool is exhausted"));
			}
			index = state_->available.back();
			state_->available.pop_back();
			data = state_->slots[index].get();
		}

		auto state = state_;
		auto lease = std::shared_ptr<void>(data, [state, index](void*) noexcept {
			std::scoped_lock lock(state->mutex);
			state->available.push_back(index);
		});
		return TensorBuffer::share(std::move(lease), data, state_->bufferCapacity);
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
		std::vector<std::unique_ptr<std::byte[]>> slots;
		std::vector<std::size_t> available;
		std::size_t bufferCapacity = 0;
	};

	explicit TensorBufferPool(std::shared_ptr<State> state)
		: state_(std::move(state)) {}

	[[nodiscard]] static Result<TensorBufferPool> invalidArgument(std::string message) {
		return Result<TensorBufferPool>::failure(
			Status::error(StatusCode::InvalidArgument, std::move(message)));
	}

	std::shared_ptr<State> state_;
};

} // namespace visonRuntime::core