#pragma once

#include "core/tensorBuffer.hpp"

#include <cstddef>

namespace visionRuntime::memory {

class GpuAllocator {
public:
	explicit GpuAllocator(int deviceIndex = 0) noexcept
		: deviceIndex_(deviceIndex) {}

	[[nodiscard]] core::Result<core::TensorBuffer> allocate(
		std::size_t byteSize) const;

	[[nodiscard]] int deviceIndex() const noexcept { return deviceIndex_; }

private:
	int deviceIndex_ = 0;
};

} // namespace visionRuntime::memory