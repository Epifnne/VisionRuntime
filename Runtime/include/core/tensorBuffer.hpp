/**
 * @file tensorBuffer.hpp
 * @author epifnne
 * @date 2026-08-10
 * @brief Defines shared tensor storage with device, memory-kind, access, and lifetime metadata.
 */

#pragma once

#include "core/device.hpp"
#include "core/result.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace visionRuntime::core {

enum class MemoryKind {
	Host,
	HostPinned,
	Device
};

class TensorBuffer {
public:
	using Deleter = std::function<void(void*)>;

	TensorBuffer() = default;

	[[nodiscard]] static Result<TensorBuffer> share(
		std::shared_ptr<void> owner,
		void* data,
		std::size_t capacity,
		Device device = Device::cpu(),
		MemoryKind memoryKind = MemoryKind::Host,
		bool writable = true) {
		if (!owner || data == nullptr) {
			return invalidArgument("buffer owner and data must not be null");
		}
		if (capacity == 0) {
			return invalidArgument("buffer capacity must be greater than zero");
		}
		if (!device.isValid()) {
			return invalidArgument("buffer device is invalid");
		}
		if (memoryKind != MemoryKind::Device && device.type != DeviceType::Cpu) {
			return invalidArgument("host memory must use a CPU device");
		}
		return Result<TensorBuffer>::success(TensorBuffer(
			std::move(owner), data, capacity, device, memoryKind, writable));
	}

	[[nodiscard]] static Result<TensorBuffer> takeOwnership(
		void* data,
		std::size_t capacity,
		Deleter deleter,
		Device device = Device::cpu(),
		MemoryKind memoryKind = MemoryKind::Host,
		bool writable = true) {
		if (data == nullptr) {
			return invalidArgument("buffer data must not be null");
		}
		if (!deleter) {
			return invalidArgument("owned buffer requires a deleter");
		}
		auto owner = std::shared_ptr<void>(data, std::move(deleter));
		return share(std::move(owner), data, capacity, device, memoryKind, writable);
	}

	[[nodiscard]] Result<TensorBuffer> slice(
		std::size_t byteOffset, std::size_t capacity) const {
		if (empty() || capacity == 0 || byteOffset > capacity_ ||
			capacity > capacity_ - byteOffset) {
			return invalidArgument("buffer slice exceeds its capacity");
		}
		return Result<TensorBuffer>::success(TensorBuffer(
			owner_, static_cast<std::byte*>(data_) + byteOffset, capacity,
			device_, memoryKind_, writable_));
	}

	[[nodiscard]] bool empty() const noexcept { return !owner_; }
	[[nodiscard]] explicit operator bool() const noexcept { return !empty(); }
	[[nodiscard]] void* data() noexcept { return writable_ ? data_ : nullptr; }
	[[nodiscard]] const void* data() const noexcept { return data_; }
	[[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
	[[nodiscard]] Device device() const noexcept { return device_; }
	[[nodiscard]] MemoryKind memoryKind() const noexcept { return memoryKind_; }
	[[nodiscard]] bool isHostAccessible() const noexcept {
		return memoryKind_ != MemoryKind::Device;
	}
	[[nodiscard]] bool isWritable() const noexcept { return writable_; }

private:
	TensorBuffer(
		std::shared_ptr<void> owner,
		void* data,
		std::size_t capacity,
		Device device,
		MemoryKind memoryKind,
		bool writable)
		: owner_(std::move(owner)),
		  data_(data),
		  capacity_(capacity),
		  device_(device),
		  memoryKind_(memoryKind),
		  writable_(writable) {}

	[[nodiscard]] static Result<TensorBuffer> invalidArgument(std::string message) {
		return Result<TensorBuffer>::failure(
			Status::error(StatusCode::InvalidArgument, std::move(message)));
	}

	std::shared_ptr<void> owner_;
	void* data_ = nullptr;
	std::size_t capacity_ = 0;
	Device device_ = Device::cpu();
	MemoryKind memoryKind_ = MemoryKind::Host;
	bool writable_ = false;
};

} // namespace visionRuntime::core