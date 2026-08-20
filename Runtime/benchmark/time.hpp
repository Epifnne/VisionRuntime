#pragma once

#include <chrono>
#include <cstddef>

namespace visionRuntime::benchmark {

using Clock = std::chrono::steady_clock;

struct PipelineDurations {
	double preprocessMilliseconds = 0.0;
	double inferenceMilliseconds = 0.0;
	double postprocessMilliseconds = 0.0;
	double stageMilliseconds = 0.0;
	double waitMilliseconds = 0.0;
	double latencyMilliseconds = 0.0;
};

struct BatchPerformance {
	std::size_t completed = 0;
	std::size_t failed = 0;
	double totalMilliseconds = 0.0;
	double framesPerSecond = 0.0;
};

[[nodiscard]] inline double elapsedMilliseconds(
	Clock::time_point start,
	Clock::time_point end) {
	return std::chrono::duration<double, std::milli>(end - start).count();
}

} // namespace visionRuntime::benchmark