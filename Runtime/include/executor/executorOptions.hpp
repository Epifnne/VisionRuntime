#pragma once

#include <cstddef>

namespace visonRuntime::executor {

struct ExecutorOptions {
	std::size_t queueCapacity = 16;
};

} // namespace visonRuntime::executor