#pragma once

#include <cstddef>

namespace visionRuntime::preprocess {

struct ImageSize {
	std::size_t width = 0;
	std::size_t height = 0;
};

} // namespace visionRuntime::preprocess