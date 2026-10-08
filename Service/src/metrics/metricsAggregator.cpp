#include "metrics/metricsAggregator.hpp"

#include "endpoints/endpointRegistry.hpp"
#include "session/sessionController.hpp"

#include <utility>
#include <nlohmann/json.hpp>

namespace visionService::metrics {

namespace {

namespace benchmark = visionRuntime::benchmark;

[[nodiscard]] std::string batchPerformancePayload(
	const benchmark::BatchPerformance& performance) {
	return nlohmann::json{
		{"completed", performance.completed}, {"failed", performance.failed},
		{"totalMilliseconds", performance.totalMilliseconds},
		{"framesPerSecond", performance.framesPerSecond},
		{"stageP50Milliseconds", performance.stageP50Milliseconds},
		{"stageP95Milliseconds", performance.stageP95Milliseconds},
		{"stageP99Milliseconds", performance.stageP99Milliseconds},
		{"stageSamples", performance.stageSamples}}.dump();
}

[[nodiscard]] std::string summaryPayload(
	const session::SessionCounters& counters,
	const std::string& lastPipelineError) {
	nlohmann::json payload{
		{"received", counters.received}, {"submitted", counters.submitted},
		{"completed", counters.completed}, {"failed", counters.failed},
		{"dropped", counters.dropped}, {"sourceFailures", counters.sourceFailures},
		{"receivedPerSource", counters.receivedPerSource},
		{"droppedPerSource", counters.droppedPerSource},
		{"missingFrames", counters.missingFrames},
		{"missingPerSource", counters.missingPerSource},
		{"rejected", counters.rejected}, {"expired", counters.expired}};
	if (!lastPipelineError.empty()) {
		payload["pipelineError"] = lastPipelineError;
	}
	return payload.dump();
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
		batchReports_.fetch_add(1, std::memory_order_relaxed);
		{
			std::lock_guard lock(payloadMutex_);
			lastBatchPayload_ = batchPerformancePayload(performance);
			livePerformancePayload_.clear();
		}
		static_cast<void>(registry_.publishState("metrics.performance"));
	};
}

visionRuntime::executor::FrameOutcomeCallback<
	visionRuntime::vision::AnomalyResult> MetricsAggregator::resultObserver() {
	return [this](visionRuntime::executor::FrameOutcome outcome,
		const visionRuntime::pipeline::FrameIdentity& identity,
		std::optional<visionRuntime::executor::TaskId> taskId,
		const visionRuntime::core::Result<visionRuntime::vision::AnomalyResult>& result) {
		using visionRuntime::executor::FrameOutcome;
		const auto deliveredAt = benchmark::Clock::now();
		const auto capturedAt = identity.metadata.capturedAt;
		const std::optional<double> latency =
			capturedAt != benchmark::Clock::time_point{} && deliveredAt >= capturedAt
			? std::optional<double>(benchmark::elapsedMilliseconds(capturedAt, deliveredAt))
			: std::nullopt;
		nlohmann::json payload{
			{"outcome", outcome == FrameOutcome::Completed ? "completed" :
				outcome == FrameOutcome::Dropped ? "dropped" : "rejected"},
			{"executionId", identity.executionId},
			{"sequenceNumber", identity.metadata.sequenceNumber},
			{"capturedAtNanoseconds", std::chrono::duration_cast<std::chrono::nanoseconds>(
				identity.metadata.capturedAt.time_since_epoch()).count()},
			{"valid", static_cast<bool>(result)},
			{"decision", "UNKNOWN"}, {"score", nullptr},
			{"sourceId", identity.sourceId ? nlohmann::json(*identity.sourceId) : nlohmann::json(nullptr)},
			{"taskId", taskId ? nlohmann::json(*taskId) : nlohmann::json(nullptr)}};
		payload["latencyMilliseconds"] =
			latency ? nlohmann::json(*latency) : nlohmann::json(nullptr);
		payload["deliveredAtNanoseconds"] = std::chrono::duration_cast<std::chrono::nanoseconds>(
			deliveredAt.time_since_epoch()).count();
		if (result) {
			payload["score"] = result->score;
			payload["threshold"] = result->threshold;
			payload["decision"] = visionRuntime::vision::anomalyDecisionName(result->decision);
		} else {
			payload["errorCode"] = visionRuntime::core::statusCodeName(result.status().code());
			payload["error"] = result.status().toString();
			std::lock_guard lock(errorMutex_);
			lastPipelineError_ = result.status().toString();
		}
		const auto text = payload.dump();
		{
			std::lock_guard lock(payloadMutex_);
			resultPayload_ = text;
			if (latency) {
				deliveryLatencies_.observe(*latency);
			}
		}
		auto published = registry_.publishState("inspection.result", {0, text});
		if (!published) {
			visionRuntime::logs::report(published.status());
		}
	};
}

void MetricsAggregator::observeSourceFault(const visionRuntime::camera::SourceFault& fault) {
	nlohmann::json payload{
		{"sourceId", fault.sourceId}, {"terminal", fault.terminal},
		{"errorCode", visionRuntime::core::statusCodeName(fault.status.code())},
		{"error", fault.status.toString()}, {"missingFrames", fault.missingFrames}};
	if (fault.previousSequence) {
		payload["previousSequence"] = *fault.previousSequence;
	}
	if (fault.currentSequence) {
		payload["currentSequence"] = *fault.currentSequence;
	}
	const auto text = payload.dump();
	{
		std::lock_guard lock(payloadMutex_);
		sourceFaultPayload_ = text;
	}
	auto published = registry_.publishState("source.fault", {0, text});
	if (!published) {
		visionRuntime::logs::report(published.status());
	}
}

std::string MetricsAggregator::resultPayload() const {
	std::lock_guard lock(payloadMutex_);
	return resultPayload_;
}

std::string MetricsAggregator::sourceFaultPayload() const {
	std::lock_guard lock(payloadMutex_);
	return sourceFaultPayload_;
}

void MetricsAggregator::reset() {
	std::scoped_lock lock(payloadMutex_, errorMutex_);
	timingCompleted_ = 0;
	timingFailed_ = 0;
	batchReports_ = 0;
	lastPipelineError_.clear();
	liveSummaryPayload_.clear();
	livePerformancePayload_.clear();
	lastBatchPayload_ = "{}";
	resultPayload_ = R"({"valid":false,"outcome":"none","decision":"UNKNOWN"})";
	sourceFaultPayload_ = "{}";
	deliveryLatencies_ = {};
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
	auto payload = nlohmann::json::parse(livePerformancePayload_.empty()
		? lastBatchPayload_ : livePerformancePayload_);
	const auto delivery = deliveryLatencies_.snapshot();
	payload["deliverySamples"] = delivery.samples;
	payload["stageMetricsAvailable"] = payload.contains("stageSamples");
	payload["deliveryP50Milliseconds"] = delivery.p50Milliseconds;
	payload["deliveryP95Milliseconds"] = delivery.p95Milliseconds;
	payload["deliveryP99Milliseconds"] = delivery.p99Milliseconds;
	return payload.dump();
}

std::string MetricsAggregator::summaryPayloadForController() const {
	std::lock_guard lock(payloadMutex_);
	if (!liveSummaryPayload_.empty()) {
		return liveSummaryPayload_;
	}
	return summaryPayload(sessionController_.counters(), lastPipelineError());
}

} // namespace visionService::metrics
