#pragma once

namespace visionRuntime::core {

enum class DeviceType {
	Cpu,
	Cuda
};

struct Device {
	DeviceType type = DeviceType::Cpu;
	int index = 0;

	[[nodiscard]] static constexpr Device cpu() noexcept {
		return {};
	}

	[[nodiscard]] static constexpr Device cuda(int index = 0) noexcept {
		return {DeviceType::Cuda, index};
	}

	[[nodiscard]] constexpr bool isValid() const noexcept {
		return index >= 0;
	}

	[[nodiscard]] constexpr bool operator==(const Device& other) const noexcept {
		return type == other.type && index == other.index;
	}
};

} // namespace visionRuntime::core