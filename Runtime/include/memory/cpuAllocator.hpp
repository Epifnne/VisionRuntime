#pragma once

#include "core/tensor.hpp"

#include <cstddef>
#include <memory>
#include <new>

namespace visionRuntime::memory {

class CpuAllocator {
public:
	[[nodiscard]] core::Result<core::TensorBuffer> allocate(
		std::size_t byteSize) const {
		if (byteSize == 0) {
			return core::Result<core::TensorBuffer>::failure(core::Status::error(
				core::StatusCode::InvalidArgument,
				"CPU allocation size must be greater than zero"));
		}
		try {
			auto owner = std::shared_ptr<void>(
				::operator new(byteSize),
				[](void* pointer) { ::operator delete(pointer); });
			return core::TensorBuffer::share(
				owner, owner.get(), byteSize, core::Device::cpu(),
				core::MemoryKind::Host);
		} catch (const std::bad_alloc&) {
			return core::Result<core::TensorBuffer>::failure(core::Status::error(
				core::StatusCode::ResourceExhausted,
				"failed to allocate CPU buffer"));
		}
	}

	[[nodiscard]] core::Result<core::Tensor> allocateTensor(
		core::DataType dataType,
		core::TensorShape shape,
		core::TensorLayout layout = core::TensorLayout::Any) const {
		auto byteSize = core::Tensor::requiredByteSize(dataType, shape);
		if (!byteSize) {
			return core::Result<core::Tensor>::failure(byteSize.status());
		}
		auto buffer = allocate(byteSize.value());
		if (!buffer) {
			return core::Result<core::Tensor>::failure(buffer.status());
		}
		return core::Tensor::wrap(
			std::move(buffer).value(), dataType, std::move(shape), layout);
	}
};

} // namespace visionRuntime::memory