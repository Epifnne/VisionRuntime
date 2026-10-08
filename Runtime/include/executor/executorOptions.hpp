#pragma once

#include <cstddef>
#include <chrono>
#include <optional>

namespace visionRuntime::executor {

enum class QueueFullPolicy {
	Drop,
	Block
};

enum class FaultPolicy {
	ReportAndContinue,
	StopSession
};

struct ExecutorOptions {
	std::size_t queueCapacity = 16;
	QueueFullPolicy queueFullPolicy = QueueFullPolicy::Drop;
	std::size_t stageQueueCapacity = 1;
	std::optional<std::chrono::milliseconds> taskTimeout{};
};

} // namespace visionRuntime::executor