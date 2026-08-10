/**
 * @file frameBufferPool.hpp
 * @author epifnne
 * @date 2026-08-10
 * @brief Defines a thread-safe fixed-capacity pool for reusable camera frame buffers.
 */

#pragma once

#include "core/tensorBufferPool.hpp"

#include <cstddef>
#include <utility>

namespace visonRuntime::camera {

class FrameBufferPool {
public:
	FrameBufferPool() = default;

	[[nodiscard]] static core::Result<FrameBufferPool> create(
		std::size_t bufferCount, std::size_t bufferCapacity) {
		auto pool = core::TensorBufferPool::create(bufferCount, bufferCapacity);
		if (!pool) {
			return core::Result<FrameBufferPool>::failure(pool.status());
		}
		return core::Result<FrameBufferPool>::success(
			FrameBufferPool(std::move(pool).value()));
	}

	[[nodiscard]] core::Result<core::TensorBuffer> acquire() const {
		return pool_.acquire();
	}

	[[nodiscard]] std::size_t size() const noexcept {
		return pool_.size();
	}

	[[nodiscard]] std::size_t bufferCapacity() const noexcept {
		return pool_.bufferCapacity();
	}

	[[nodiscard]] std::size_t available() const {
		return pool_.available();
	}

private:
	explicit FrameBufferPool(core::TensorBufferPool pool)
		: pool_(std::move(pool)) {}

	core::TensorBufferPool pool_;
};

} // namespace visonRuntime::camera