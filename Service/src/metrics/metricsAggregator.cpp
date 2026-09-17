#include "metrics/metricsAggregator.hpp"

#include "endpoints/endpointRegistry.hpp"
#include "session/sessionController.hpp"

#include <cstdio>
#include <utility>

namespace visionService::metrics {

namespace {

namespace benchmark = visionRuntime::benchmark;

[[nodiscard]] std::string batchPerformancePayload(
	const benchmark::BatchPerformance& performance) {
	char buffer[512];
	std::snprintf(buffer, sizeof(buffer),
		R"({"completed":%zu,"failed":%zu,"totalMilliseconds":%.3f,)"
		R"("framesPerSecond":%.3f,"stageP50Milliseconds":%.3f,)"
		R"("stageP95Milliseconds":%.3f,"stageP99Milliseconds":%.3f})",
		performance.completed, performance.failed,
		performance.totalMilliseconds, performance.framesPerSecond,
		performance.stageP50Milliseconds, performance.stageP95Milliseconds,
		performance.stageP99Milliseconds);
	return buffer;
}

[[nodiscard]] std::string counterArray(const std::vector<std::size_t>& values) {
	std::string payload = "[";
	for (std::size_t index = 0; index < values.size(); ++index) {
		if (index > 0) {
			payload += ',';
		}
		payload += std::to_string(values[index]);
	}
	payload += ']';
	return payload;
}

[[nodiscard]] std::string summaryPayload(
	const session::SessionCounters& counters,
	const std::string& lastPipelineError) {
	char buffer[512];
	std::snprintf(buffer, sizeof(buffer),
		R"({"received":%zu,"submitted":%zu,"completed":%zu,"failed":%zu,)"
		R"("dropped":%zu,"sourceFailures":%zu,"receivedPerSource":%s,)"
		R"("droppedPerSource":%s)",
		counters.received, counters.submitted, counters.completed,
		counters.failed, counters.dropped, counters.sourceFailures,
		counterArray(counters.receivedPerSource).c_str(),
		counterArray(counters.droppedPerSource).c_str());
	std::string payload = buffer;
	if (!lastPipelineError.empty()) {
		payload += R"(,"pipelineError":")";
		payload += lastPipelineError;
		payload += '"';
	}
	payload += '}';
	return payload;
}

} // namespace

MetricsAggregator::MetricsAggregator(
	endpoints::EndpointRegistry& registry,
	const session::SessionController& sessionController) noexcept
	: registry_(registry), sessionController_(sessionController) {}

benchmark::PipelineTimingObserver<visionRuntime::vision::AnomalyResult>
MetricsAggregator::timingObserver() {
	return [this](
		std::uint64_t sequenceNumber,
		const visionRuntime::core::Result<visionRuntime::vision::AnomalyResult>&
			result,
		const benchmark::PipelineDurations& durations) {
		static_cast<void>(sequenceNumber);
		static_cast<void>(durations);
		timingCompleted_.fetch_add(1, std::memory_order_relaxed);
		if (!result) {
			timingFailed_.fetch_add(1, std::memory_order_relaxed);
			std::lock_guard lock(errorMutex_);
			lastPipelineError_ = result.status().toString();
		}
	};
}

benchmark::BatchPerformanceObserver MetricsAggregator::batchObserver() {
	return [this](const benchmark::BatchPerformance& performance) {
		lastBatch_ = performance;
		batchReports_.fetch_add(1, std::memory_order_relaxed);
		{
			std::lock_guard lock(payloadMutex_);
			lastBatchPayload_ = batchPerformancePayload(performance);
			livePerformancePayload_.clear();
		}
		static_cast<void>(registry_.publishState("metrics.performance"));
	};
}

void MetricsAggregator::publishSessionState() {
	static_cast<void>(registry_.publishState("session.state"));
}

void MetricsAggregator::publishSummary() {
	{
		std::lock_guard lock(payloadMutex_);
		liveSummaryPayload_.clear();
	}
	static_cast<void>(registry_.publishState("session.summary"));
}

void MetricsAggregator::publishLive(
	const benchmark::TimedPipeline<visionRuntime::vision::AnomalyResult>*
		timedPipeline) {
	const auto live = sessionController_.liveCounters();
	if (live) {
		{
			std::lock_guard lock(payloadMutex_);
			liveSummaryPayload_ = summaryPayload(*live, lastPipelineError());
		}
		static_cast<void>(registry_.publishState("session.summary"));
	}
	if (timedPipeline != nullptr) {
		const auto snapshot = timedPipeline->livePerformanceSnapshot();
		if (snapshot) {
			{
				std::lock_guard lock(payloadMutex_);
				livePerformancePayload_ = batchPerformancePayload(*snapshot);
			}
			static_cast<void>(registry_.publishState("metrics.performance"));
		}
	}
}

std::uint64_t MetricsAggregator::completedCount() const noexcept {
	return timingCompleted_.load(std::memory_order_relaxed);
}

std::string MetricsAggregator::lastPipelineError() const {
	std::lock_guard lock(errorMutex_);
	return lastPipelineError_;
}

std::string MetricsAggregator::performancePayload() const {
	std::lock_guard lock(payloadMutex_);
	if (!livePerformancePayload_.empty()) {
		return livePerformancePayload_;
	}
	return lastBatchPayload_;
}

std::string MetricsAggregator::summaryPayloadForController() const {
	std::lock_guard lock(payloadMutex_);
	if (!liveSummaryPayload_.empty()) {
		return liveSummaryPayload_;
	}
	return summaryPayload(sessionController_.counters(), lastPipelineError());
}

} // namespace visionService::metrics
