#include "memory/gpuAllocator.hpp"

#include <cuda_runtime_api.h>

#include <sstream>
#include <string>

namespace visionRuntime::memory {
namespace {

[[nodiscard]] core::Status cudaError(const char* operation, cudaError_t status) {
	std::ostringstream message;
	message << operation << " failed: " << cudaGetErrorString(status);
	return core::Status::error(core::StatusCode::BackendError, message.str());
}

} // namespace

core::Result<core::TensorBuffer> GpuAllocator::allocate(
	std::size_t byteSize) const {
	if (byteSize == 0) {
		return core::Result<core::TensorBuffer>::failure(core::Status::error(
			core::StatusCode::InvalidArgument,
			"GPU allocation size must be greater than zero"));
	}
	if (deviceIndex_ < 0) {
		return core::Result<core::TensorBuffer>::failure(core::Status::error(
			core::StatusCode::InvalidArgument,
			"GPU device index must not be negative"));
	}

	auto status = cudaSetDevice(deviceIndex_);
	if (status != cudaSuccess) {
		return core::Result<core::TensorBuffer>::failure(
			cudaError("CUDA device selection", status));
	}

	void* data = nullptr;
	status = cudaMalloc(&data, byteSize);
	if (status != cudaSuccess) {
		return core::Result<core::TensorBuffer>::failure(
			cudaError("CUDA device buffer allocation", status));
	}

	const auto deviceIndex = deviceIndex_;
	return core::TensorBuffer::takeOwnership(
		data, byteSize,
		[deviceIndex](void* pointer) noexcept {
			int previousDevice = -1;
			const auto hasPreviousDevice = cudaGetDevice(&previousDevice) == cudaSuccess;
			static_cast<void>(cudaSetDevice(deviceIndex));
			static_cast<void>(cudaFree(pointer));
			if (hasPreviousDevice && previousDevice != deviceIndex) {
				static_cast<void>(cudaSetDevice(previousDevice));
			}
		},
		core::Device::cuda(deviceIndex), core::MemoryKind::Device);
}

} // namespace visionRuntime::memory