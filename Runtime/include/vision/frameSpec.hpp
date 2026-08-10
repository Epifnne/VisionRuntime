#pragma once

#include "core/device.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <vector>

namespace visonRuntime::vision {

enum class PixelFormat {
	Gray8,
	Gray16,
	Bgr8,
	Rgb8,
	Bgra8,
	Rgba8,
	Float32Gray
};

[[nodiscard]] constexpr std::size_t pixelFormatSize(PixelFormat pixelFormat) noexcept {
	switch (pixelFormat) {
	case PixelFormat::Gray8:
		return 1;
	case PixelFormat::Gray16:
		return 2;
	case PixelFormat::Bgr8:
	case PixelFormat::Rgb8:
		return 3;
	case PixelFormat::Bgra8:
	case PixelFormat::Rgba8:
	case PixelFormat::Float32Gray:
		return 4;
	}
	return 0;
}

struct FrameSpec {
	std::vector<PixelFormat> pixelFormats;
	std::optional<std::size_t> width;
	std::optional<std::size_t> height;
	std::optional<core::Device> device;

	[[nodiscard]] bool isValid() const noexcept {
		return (!width || *width > 0) && (!height || *height > 0)
			&& (!device || device->isValid());
	}

	[[nodiscard]] bool accepts(const FrameSpec& produced) const noexcept {
		if (!isValid() || !produced.isValid()) {
			return false;
		}
		if (!pixelFormats.empty()) {
			if (produced.pixelFormats.empty()) {
				return false;
			}
			for (const auto format : produced.pixelFormats) {
				if (std::ranges::find(pixelFormats, format) == pixelFormats.end()) {
					return false;
				}
			}
		}
		if (width && (!produced.width || width != produced.width)) {
			return false;
		}
		if (height && (!produced.height || height != produced.height)) {
			return false;
		}
		if (device && (!produced.device || device != produced.device)) {
			return false;
		}
		return true;
	}
};

} // namespace visonRuntime::vision