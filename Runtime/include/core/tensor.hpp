/**
 * @file tensor.hpp
 * @author epifnne
 * @date 2026-08-10
 * @brief Defines typed multidimensional tensor views over shared memory buffers.
 */

#pragma once

#include "core/dataType.hpp"
#include "core/device.hpp"
#include "core/result.hpp"
#include "core/tensorBuffer.hpp"
#include "core/tensorShape.hpp"
#include "core/tensorSpec.hpp"

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace visionRuntime::core {

enum class TensorOwnership {
	Owned,
	Shared,
	Borrowed
};

class Tensor {
public:
	using ByteStrides = std::vector<std::size_t>;
	using Deleter = std::function<void(void*)>;

	Tensor() = default;

	[[nodiscard]] static Result<Tensor> allocate(
		DataType dataType,
		TensorShape shape,
		TensorLayout layout = TensorLayout::Any) {
		auto metadata = validateMetadata(dataType, shape, {});
		if (!metadata) {
			return Result<Tensor>::failure(metadata.status());
		}

		auto buffer = TensorBuffer::allocate(metadata->byteSize);
		if (!buffer) {
			return Result<Tensor>::failure(buffer.status());
		}

		return Result<Tensor>::success(Tensor(
			std::move(buffer).value(), 0, metadata->byteSize, dataType, std::move(shape),
			std::move(metadata->byteStrides), Device::cpu(), layout,
			TensorOwnership::Owned));
	}

	[[nodiscard]] static Result<Tensor> wrap(
		TensorBuffer buffer,
		DataType dataType,
		TensorShape shape,
		TensorLayout layout = TensorLayout::Any,
		ByteStrides byteStrides = {},
		std::size_t byteOffset = 0) {
		return create(
			std::move(buffer), byteOffset, dataType, std::move(shape), layout,
			std::move(byteStrides), TensorOwnership::Shared);
	}

	[[nodiscard]] static Result<Tensor> share(
		std::shared_ptr<void> storage,
		std::size_t byteCapacity,
		DataType dataType,
		TensorShape shape,
		Device device = Device::cpu(),
		TensorLayout layout = TensorLayout::Any,
		ByteStrides byteStrides = {}) {
		if (!storage) {
			return invalidArgument("tensor storage must not be null");
		}
		auto buffer = TensorBuffer::share(
			storage, storage.get(), byteCapacity, device,
			device.type == DeviceType::Cpu ? MemoryKind::Host : MemoryKind::Device);
		if (!buffer) {
			return Result<Tensor>::failure(buffer.status());
		}
		return create(std::move(buffer).value(), 0, dataType, std::move(shape),
			layout, std::move(byteStrides), TensorOwnership::Shared);
	}

	[[deprecated("borrow does not extend buffer lifetime; use TensorBuffer and wrap")]]
	[[nodiscard]] static Result<Tensor> borrow(
		void* data,
		std::size_t byteCapacity,
		DataType dataType,
		TensorShape shape,
		Device device = Device::cpu(),
		TensorLayout layout = TensorLayout::Any,
		ByteStrides byteStrides = {}) {
		if (data == nullptr) {
			return invalidArgument("tensor data must not be null");
		}
		auto owner = std::shared_ptr<void>(data, [](void*) {});
		auto buffer = TensorBuffer::share(
			std::move(owner), data, byteCapacity, device,
			device.type == DeviceType::Cpu ? MemoryKind::Host : MemoryKind::Device);
		if (!buffer) {
			return Result<Tensor>::failure(buffer.status());
		}
		return create(std::move(buffer).value(), 0, dataType, std::move(shape),
			layout, std::move(byteStrides), TensorOwnership::Borrowed);
	}

	[[nodiscard]] static Result<Tensor> takeOwnership(
		void* data,
		std::size_t byteCapacity,
		DataType dataType,
		TensorShape shape,
		Deleter deleter,
		Device device = Device::cpu(),
		TensorLayout layout = TensorLayout::Any,
		ByteStrides byteStrides = {}) {
		if (data == nullptr) {
			return invalidArgument("tensor data must not be null");
		}
		if (!deleter) {
			return invalidArgument("owned tensor requires a deleter");
		}
		auto buffer = TensorBuffer::takeOwnership(
			data, byteCapacity, std::move(deleter), device,
			device.type == DeviceType::Cpu ? MemoryKind::Host : MemoryKind::Device);
		if (!buffer) {
			return Result<Tensor>::failure(buffer.status());
		}
		return create(std::move(buffer).value(), 0, dataType, std::move(shape),
			layout, std::move(byteStrides), TensorOwnership::Owned);
	}

	[[nodiscard]] Result<Tensor> subview(
		std::size_t relativeByteOffset,
		DataType dataType,
		TensorShape shape,
		TensorLayout layout = TensorLayout::Any,
		ByteStrides byteStrides = {}) const {
		if (relativeByteOffset > byteSize_) {
			return invalidArgument("tensor subview offset exceeds its byte size");
		}
		auto view = create(
			buffer_, byteOffset_ + relativeByteOffset, dataType, std::move(shape),
			layout, std::move(byteStrides), TensorOwnership::Shared);
		if (view && view->byteSize() > byteSize_ - relativeByteOffset) {
			return invalidArgument("tensor subview exceeds its parent view");
		}
		return view;
	}

	[[nodiscard]] bool empty() const noexcept { return !buffer_; }
	[[nodiscard]] explicit operator bool() const noexcept { return !empty(); }
	[[nodiscard]] void* data() noexcept {
		if (empty() || !buffer_.isWritable()) {
			return nullptr;
		}
		return static_cast<std::byte*>(buffer_.data()) + byteOffset_;
	}
	[[nodiscard]] const void* data() const noexcept {
		if (empty()) {
			return nullptr;
		}
		return static_cast<const std::byte*>(buffer_.data()) + byteOffset_;
	}
	[[nodiscard]] std::size_t byteSize() const noexcept { return byteSize_; }
	[[nodiscard]] std::size_t byteOffset() const noexcept { return byteOffset_; }
	[[nodiscard]] std::size_t capacity() const noexcept { return buffer_.capacity(); }
	[[nodiscard]] std::size_t elementCount() const noexcept { return elementCount_; }
	[[nodiscard]] DataType dataType() const noexcept { return dataType_; }
	[[nodiscard]] const TensorShape& shape() const noexcept { return shape_; }
	[[nodiscard]] const ByteStrides& byteStrides() const noexcept { return byteStrides_; }
	[[nodiscard]] Device device() const noexcept { return device_; }
	[[nodiscard]] TensorLayout layout() const noexcept { return layout_; }
	[[nodiscard]] TensorOwnership ownership() const noexcept { return ownership_; }
	[[nodiscard]] const TensorBuffer& buffer() const noexcept { return buffer_; }
	[[nodiscard]] MemoryKind memoryKind() const noexcept { return buffer_.memoryKind(); }
	[[nodiscard]] bool isWritable() const noexcept { return buffer_.isWritable(); }

	[[nodiscard]] bool isContiguous() const noexcept {
		return byteStrides_ == contiguousByteStrides(shape_, dataType_);
	}

	[[nodiscard]] TensorSpec spec() const {
		return {dataType_, shape_, device_, layout_};
	}

	[[nodiscard]] std::span<std::byte> bytes() noexcept {
		if (!buffer_.isHostAccessible() || !buffer_.isWritable()) {
			return {};
		}
		return {static_cast<std::byte*>(data()), byteSize_};
	}

	[[nodiscard]] std::span<const std::byte> bytes() const noexcept {
		if (!buffer_.isHostAccessible()) {
			return {};
		}
		return {static_cast<const std::byte*>(data()), byteSize_};
	}

private:
	struct Metadata {
		std::size_t elementCount;
		std::size_t byteSize;
		ByteStrides byteStrides;
	};

	Tensor(
		TensorBuffer buffer,
		std::size_t byteOffset,
		std::size_t byteSize,
		DataType dataType,
		TensorShape shape,
		ByteStrides byteStrides,
		Device device,
		TensorLayout layout,
		TensorOwnership ownership)
		: buffer_(std::move(buffer)),
		  byteOffset_(byteOffset),
		  byteSize_(byteSize),
		  elementCount_(elementCount(shape)),
		  dataType_(dataType),
		  shape_(std::move(shape)),
		  byteStrides_(std::move(byteStrides)),
		  device_(device),
		  layout_(layout),
		  ownership_(ownership) {}

	[[nodiscard]] static Result<Tensor> create(
		TensorBuffer buffer,
		std::size_t byteOffset,
		DataType dataType,
		TensorShape shape,
		TensorLayout layout,
		ByteStrides byteStrides,
		TensorOwnership ownership) {
		if (!buffer) {
			return invalidArgument("tensor buffer must not be empty");
		}
		auto metadata = validateMetadata(dataType, shape, std::move(byteStrides));
		if (!metadata) {
			return Result<Tensor>::failure(metadata.status());
		}
		if (byteOffset > buffer.capacity() ||
			metadata->byteSize > buffer.capacity() - byteOffset) {
			return invalidArgument("tensor buffer capacity is smaller than its layout");
		}
		const auto device = buffer.device();
		return Result<Tensor>::success(Tensor(
			std::move(buffer), byteOffset, metadata->byteSize, dataType,
			std::move(shape), std::move(metadata->byteStrides),
			device, layout, ownership));
	}

	[[nodiscard]] static Result<Metadata> validateMetadata(
		DataType dataType,
		const TensorShape& shape,
		ByteStrides byteStrides) {
		if (!shape.isConcrete()) {
			return Result<Metadata>::failure(Status::error(
				StatusCode::InvalidArgument, "tensor shape must be concrete"));
		}
		const auto count = elementCount(shape);
		if (count == 0 || count > std::numeric_limits<std::size_t>::max() / dataTypeSize(dataType)) {
			return Result<Metadata>::failure(Status::error(
				StatusCode::InvalidArgument, "tensor element count overflows size_t"));
		}
		if (byteStrides.empty()) {
			byteStrides = contiguousByteStrides(shape, dataType);
		}
		if (byteStrides.size() != shape.rank()) {
			return Result<Metadata>::failure(Status::error(
				StatusCode::InvalidArgument, "tensor stride rank does not match shape rank"));
		}

		std::size_t requiredBytes = dataTypeSize(dataType);
		for (std::size_t index = 0; index < shape.rank(); ++index) {
			const auto extent = static_cast<std::size_t>(shape.dimensions()[index] - 1);
			if (extent != 0 && byteStrides[index] >
				(std::numeric_limits<std::size_t>::max() - requiredBytes) / extent) {
				return Result<Metadata>::failure(Status::error(
					StatusCode::InvalidArgument, "tensor byte strides overflow size_t"));
			}
			requiredBytes += extent * byteStrides[index];
		}
		return Result<Metadata>::success(Metadata{count, requiredBytes, std::move(byteStrides)});
	}

	[[nodiscard]] static std::size_t elementCount(const TensorShape& shape) noexcept {
		std::size_t count = 1;
		for (const auto dimension : shape.dimensions()) {
			if (dimension <= 0 || count >
				std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(dimension)) {
				return 0;
			}
			count *= static_cast<std::size_t>(dimension);
		}
		return count;
	}

	[[nodiscard]] static ByteStrides contiguousByteStrides(
		const TensorShape& shape, DataType dataType) {
		ByteStrides strides(shape.rank());
		std::size_t stride = dataTypeSize(dataType);
		for (std::size_t index = shape.rank(); index > 0; --index) {
			strides[index - 1] = stride;
			const auto dimension = shape.dimensions()[index - 1];
			if (dimension <= 0 || stride >
				std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(dimension)) {
				return {};
			}
			stride *= static_cast<std::size_t>(dimension);
		}
		return strides;
	}

	[[nodiscard]] static Result<Tensor> invalidArgument(std::string message) {
		return Result<Tensor>::failure(
			Status::error(StatusCode::InvalidArgument, std::move(message)));
	}

	TensorBuffer buffer_;
	std::size_t byteOffset_ = 0;
	std::size_t byteSize_ = 0;
	std::size_t elementCount_ = 0;
	DataType dataType_ = DataType::UInt8;
	TensorShape shape_;
	ByteStrides byteStrides_;
	Device device_ = Device::cpu();
	TensorLayout layout_ = TensorLayout::Any;
	TensorOwnership ownership_ = TensorOwnership::Borrowed;
};

} // namespace visionRuntime::core