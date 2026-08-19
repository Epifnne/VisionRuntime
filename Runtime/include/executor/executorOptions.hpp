#pragma once

#include <cstddef>

namespace visionRuntime::executor {

enum class QueueFullPolicy {
	Drop,
	Block
};

struct ExecutorOptions {
	std::size_t queueCapacity = 16;
	QueueFullPolicy queueFullPolicy = QueueFullPolicy::Drop;
};

} // namespace visionRuntime::executor