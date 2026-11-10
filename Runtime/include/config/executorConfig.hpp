#pragma once

#include <cstddef>
#include <chrono>
#include <optional>

namespace visionRuntime::config {

enum class PerformancePolicy {
	Serial,
	PipelineParallel,
	TaskParallel
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
	std::optional<std::chrono::milliseconds> taskTimeout{};
	std::size_t workerCount = 1;
	std::size_t maxInFlight = 16;
};

} // namespace visionRuntime::config