#pragma once

#include <cstdint>

namespace visonRuntime::vision {

struct ImageSize {
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};

struct ImagePadding {
	std::uint32_t left = 0;
	std::uint32_t top = 0;
	std::uint32_t right = 0;
	std::uint32_t bottom = 0;
};

struct ImageCrop {
	std::uint32_t left = 0;
	std::uint32_t top = 0;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};

struct TransformContext {
	ImageSize sourceSize;
	ImageSize networkSize;
	float scaleX = 1.0F;
	float scaleY = 1.0F;
	ImageCrop crop;
	ImagePadding padding;
};

} // namespace visonRuntime::vision