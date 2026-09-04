#pragma once

#include "memory/cpuAllocator.hpp"
#include "memory/detail/bufferPool.hpp"

#include <cstddef>
#include <memory>
#include <stop_token>
#include <utility>

namespace visionRuntime::memory {

class CpuBufferPool {
public:
	CpuBufferPool() = default;

	[[nodiscard]] static core::Result<CpuBufferPool> create(
		std::size_t bufferCount,
		std::size_t bufferCapacity,
		std::shared_ptr<CpuAllocator> allocator = std::make_shared<CpuAllocator>()) {
		auto pool = detail::BufferPool::create(
			bufferCount, bufferCapacity, std::move(allocator));
		if (!pool) {
			return core::Result<CpuBufferPool>::failure(pool.status());
		}
		return core::Result<CpuBufferPool>::success(
			CpuBufferPool(std::move(pool).value()));
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
	explicit CpuBufferPool(detail::BufferPool pool)
		: pool_(std::move(pool)) {}

	detail::BufferPool pool_;
};

} // namespace visionRuntime::memory