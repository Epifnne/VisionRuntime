#pragma once

#include "memory/detail/bufferPool.hpp"
#include "memory/gpuAllocator.hpp"

#include <cstddef>
#include <memory>
#include <stop_token>
#include <utility>

namespace visionRuntime::memory {

class GpuBufferPool {
public:
	GpuBufferPool() = default;

	[[nodiscard]] static core::Result<GpuBufferPool> create(
		std::size_t bufferCount,
		std::size_t bufferCapacity,
		std::shared_ptr<GpuAllocator> allocator) {
		auto pool = detail::BufferPool::create(
			bufferCount, bufferCapacity, std::move(allocator));
		if (!pool) {
			return core::Result<GpuBufferPool>::failure(pool.status());
		}
		return core::Result<GpuBufferPool>::success(
			GpuBufferPool(std::move(pool).value()));
	}

	[[nodiscard]] static core::Result<GpuBufferPool> create(
		std::size_t bufferCount, std::size_t bufferCapacity, int deviceIndex = 0) {
		return create(bufferCount, bufferCapacity,
			std::make_shared<GpuAllocator>(deviceIndex));
	}

	[[nodiscard]] core::Result<core::TensorBuffer> acquire() const {
		return pool_.acquire();
	}

	[[nodiscard]] core::Result<core::TensorBuffer> acquireBlocking(
		std::stop_token stopToken = {}) const {
		return pool_.acquireBlocking(stopToken);
	}

	[[nodiscard]] std::size_t size() const noexcept { return pool_.size(); }
	[[nodiscard]] std::size_t bufferCapacity() const noexcept {
		return pool_.bufferCapacity();
	}
	[[nodiscard]] std::size_t available() const { return pool_.available(); }

private:
	explicit GpuBufferPool(detail::BufferPool pool)
		: pool_(std::move(pool)) {}

	detail::BufferPool pool_;
};

} // namespace visionRuntime::memory