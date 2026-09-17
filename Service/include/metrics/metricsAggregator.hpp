/**
 * @file metricsAggregator.hpp
 * @brief Aggregates pipeline timing and batch performance into State endpoints.
 *
 * Hooks TimedPipeline observers (called from pipeline worker threads) and the
 * session controller; every observation is republished through the endpoint
 * registry, so delivery to subscribers always happens on the service dispatch
 * thread.
 */

#pragma once

#include "benchmark/time.hpp"
#include "benchmark/timedPipeline.hpp"
#include "vision/anomalyResult.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace visionService {

namespace endpoints {
class EndpointRegistry;
}

namespace session {
class SessionController;
}

namespace metrics {

class MetricsAggregator {
public:
	MetricsAggregator(
		endpoints::EndpointRegistry& registry,
		const session::SessionController& sessionController) noexcept;

	[[nodiscard]] visionRuntime::benchmark::PipelineTimingObserver<
		visionRuntime::vision::AnomalyResult>
	timingObserver();
	[[nodiscard]] visionRuntime::benchmark::BatchPerformanceObserver
	batchObserver();

	void publishSessionState();
	void publishSummary();
	/// Refreshes `session.summary` / `metrics.performance` payloads with live
	/// counters (and the pipeline snapshot when one is attached), then
	/// publishes both. Called periodically while running; final values still
	/// flow through publishSummary()/batchObserver at batch end.
	void publishLive(
		const visionRuntime::benchmark::TimedPipeline<
			visionRuntime::vision::AnomalyResult>* timedPipeline);

	[[nodiscard]] std::string summaryPayloadForController() const;
	[[nodiscard]] std::string performancePayload() const;

	[[nodiscard]] std::uint64_t completedCount() const noexcept;
	[[nodiscard]] std::string lastPipelineError() const;

private:

	endpoints::EndpointRegistry& registry_;
	const session::SessionController& sessionController_;
	std::atomic_uint64_t timingCompleted_{0};
	std::atomic_uint64_t timingFailed_{0};
	std::atomic_uint64_t batchReports_{0};
	mutable std::mutex errorMutex_;
	std::string lastPipelineError_;
	visionRuntime::benchmark::BatchPerformance lastBatch_;
	/// Guards lastBatchPayload_ / liveSummaryPayload_ / livePerformancePayload_
	/// (written by pipeline, control and metrics threads; read by endpoint
	/// snapshot calls from any thread).
	mutable std::mutex payloadMutex_;
	std::string lastBatchPayload_ =
		R"({"completed":0,"failed":0,"totalMilliseconds":0,)"
		R"("framesPerSecond":0,"stageP50Milliseconds":0,)"
		R"("stageP95Milliseconds":0,"stageP99Milliseconds":0})";
	/// Live payloads while a run is in flight; cleared by publishSummary()
	/// and batchObserver (final values win).
	std::string liveSummaryPayload_;
	std::string livePerformancePayload_;
};

} // namespace metrics
} // namespace visionService
