/**
 * @file frameBufferPool.hpp
 * @author epifnne
 * @date 2026-08-10
 * @brief Defines a thread-safe fixed-capacity pool for reusable camera frame buffers.
 */

#pragma once

#include "core/tensorBufferPool.hpp"

#include <cstddef>
#include <stop_token>
#include <utility>

namespace visionRuntime::camera {

enum class BufferFullPolicy {
	Drop,
	Block
};

struct FrameBufferPoolOptions {
	std::size_t bufferCount = 3;
	std::size_t bufferCapacity = 0;
	BufferFullPolicy fullPolicy = BufferFullPolicy::Drop;
};

class FrameBufferPool {
public:
	FrameBufferPool() = default;

	[[nodiscard]] static core::Result<FrameBufferPool> create(
		FrameBufferPoolOptions options) {
		auto pool = core::TensorBufferPool::create(
			options.bufferCount, options.bufferCapacity);
		if (!pool) {
			return core::Result<FrameBufferPool>::failure(pool.status());
		}
		return core::Result<FrameBufferPool>::success(FrameBufferPool(
			std::move(pool).value(), options.fullPolicy));
	}

	[[nodiscard]] static core::Result<FrameBufferPool> create(
		std::size_t bufferCount, std::size_t bufferCapacity) {
		return create({bufferCount, bufferCapacity, BufferFullPolicy::Drop});
	}

	[[nodiscard]] core::Result<core::TensorBuffer> acquire() const {
		return acquire({});
	}

	[[nodiscard]] core::Result<core::TensorBuffer> acquire(
		std::stop_token stopToken) const {
		if (fullPolicy_ == BufferFullPolicy::Block) {
			return pool_.acquireBlocking(stopToken);
		}
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
	explicit FrameBufferPool(
		core::TensorBufferPool pool, BufferFullPolicy fullPolicy)
		: pool_(std::move(pool)), fullPolicy_(fullPolicy) {}

	core::TensorBufferPool pool_;
	BufferFullPolicy fullPolicy_ = BufferFullPolicy::Drop;
};

} // namespace visionRuntime::camera