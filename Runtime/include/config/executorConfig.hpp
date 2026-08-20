#pragma once

#include <cstddef>

namespace visionRuntime::config {

enum class PerformancePolicy {
	Serial,
	PipelineParallel
};

enum class QueueFullPolicy {
	Drop,
	Block
};

struct ExecutorConfig {
	PerformancePolicy performancePolicy = PerformancePolicy::Serial;
	QueueFullPolicy queueFullPolicy = QueueFullPolicy::Drop;
	std::size_t queueCapacity = 16;
	std::size_t stageQueueCapacity = 1;
};

} // namespace visionRuntime::config