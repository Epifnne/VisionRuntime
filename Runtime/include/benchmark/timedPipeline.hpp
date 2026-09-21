#pragma once

#include "benchmark/time.hpp"
#include "core/result.hpp"
#include "pipeline/iStagedVisionPipeline.hpp"
#include "pipeline/pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace visionRuntime::benchmark {

template<typename ResultType>
using PipelineTimingObserver = std::function<void(
	std::uint64_t sequenceNumber,
	const core::Result<ResultType>& result,
	const PipelineDurations& durations)>;

using BatchPerformanceObserver = std::function<void(
	const BatchPerformance& performance)>;

template<typename ResultType>
class TimedPipeline final : public pipeline::IStagedVisionPipeline<ResultType> {
public:
	TimedPipeline(
		pipeline::Pipeline<ResultType> pipeline,
		PipelineTimingObserver<ResultType> observer,
		BatchPerformanceObserver batchObserver = {})
		: pipeline_(std::move(pipeline)), observer_(std::move(observer)),
		  batchObserver_(std::move(batchObserver)) {}

	[[nodiscard]] core::Result<ResultType> run(
		pipeline::PipelinePacket packet) override {
		const auto executionId = packet.executionId();
		auto prepared = preprocess(std::move(packet));
		if (!prepared) {
			auto result = core::Result<ResultType>::failure(prepared.status());
			finishExecution(executionId, result);
			return result;
		}
		auto output = infer(std::move(prepared).value());
		if (!output) {
			auto result = core::Result<ResultType>::failure(output.status());
			finishExecution(executionId, result);
			return result;
		}
		auto result = postprocess(std::move(output).value());
		finishExecution(executionId, result);
		return result;
	}

	[[nodiscard]] core::Result<preprocess::PreparedInput> preprocess(
		pipeline::PipelinePacket packet) override {
		const auto executionId = packet.executionId();
		const auto* frame = packet.cameraFrame();
		const auto sequenceNumber = frame == nullptr
			? 0
			: frame->metadata().sequenceNumber;
		const auto pipelineStarted = Clock::now();
		const auto preprocessStarted = Clock::now();
		{
			std::lock_guard lock(mutex_);
			if (!batchStarted_) {
				batchStarted_ = pipelineStarted;
			}
			timings_.emplace(executionId, TimingState{
				sequenceNumber, pipelineStarted, {}});
		}
		auto prepared = pipeline_.preprocess(std::move(packet));
		updateDuration(executionId, &PipelineDurations::preprocessMilliseconds,
			preprocessStarted);
		if (!prepared) {
			// Staged executors (batch) skip run() and never call
			// finishExecution for failed preprocess; account the failure
			// here. finishExecution is idempotent per executionId, so the
			// run() path is unaffected.
			finishExecution(executionId,
				core::Result<ResultType>::failure(prepared.status()));
		}
		return prepared;
	}

	[[nodiscard]] core::Result<pipeline::InferenceOutput> infer(
		preprocess::PreparedInput input) override {
		const auto executionId = input.packet().executionId();
		const auto started = Clock::now();
		auto output = pipeline_.infer(std::move(input));
		updateDuration(executionId, &PipelineDurations::inferenceMilliseconds, started);
		return output;
	}

	[[nodiscard]] core::Result<std::vector<pipeline::InferenceOutput>> inferBatch(
		std::vector<preprocess::PreparedInput> inputs) override {
		std::vector<std::uint64_t> executionIds;
		executionIds.reserve(inputs.size());
		for (const auto& input : inputs) {
			executionIds.push_back(input.packet().executionId());
		}
		const auto started = Clock::now();
		auto outputs = pipeline_.inferBatch(std::move(inputs));
		// Every member waited for the whole batch inference; record the full
		// wall time per member (latency view). Throughput is reported by the
		// batch observer, not by dividing this duration.
		const auto elapsed = elapsedMilliseconds(started, Clock::now());
		if (!outputs) {
			const auto failure = core::Result<ResultType>::failure(outputs.status());
			for (const auto executionId : executionIds) {
				finishExecution(executionId, failure);
			}
			return outputs;
		}
		std::lock_guard lock(mutex_);
		for (const auto executionId : executionIds) {
			auto timing = timings_.find(executionId);
			if (timing != timings_.end()) {
				timing->second.durations.inferenceMilliseconds = elapsed;
			}
		}
		return outputs;
	}

	[[nodiscard]] core::Result<ResultType> postprocess(
		pipeline::InferenceOutput output) override {
		const auto executionId = output.packet().executionId();
		const auto started = Clock::now();
		auto result = pipeline_.postprocess(std::move(output));
		updateDuration(executionId, &PipelineDurations::postprocessMilliseconds, started);
		return result;
	}

	void finishExecution(
		std::uint64_t executionId,
		const core::Result<ResultType>& result) noexcept override {
		std::optional<TimingState> timing;
		{
			std::lock_guard lock(mutex_);
			auto found = timings_.find(executionId);
			if (found == timings_.end()) {
				return;
			}
			timing.emplace(found->second);
			timings_.erase(found);
		}
		timing->durations.latencyMilliseconds = elapsedMilliseconds(
			timing->pipelineStarted, Clock::now());
		timing->durations.stageMilliseconds =
			timing->durations.preprocessMilliseconds +
			timing->durations.inferenceMilliseconds +
			timing->durations.postprocessMilliseconds;
		timing->durations.waitMilliseconds =
			timing->durations.latencyMilliseconds > timing->durations.stageMilliseconds
			? timing->durations.latencyMilliseconds - timing->durations.stageMilliseconds
			: 0.0;
		if (observer_) {
			try {
				observer_(timing->sequenceNumber, result, timing->durations);
			} catch (...) {
			}
		}
		std::lock_guard lock(mutex_);
		++batchCompleted_;
		batchStageDurations_.push_back(timing->durations.stageMilliseconds);
		if (!result) {
			++batchFailed_;
		}
	}

	void finishBatch() noexcept override {
		BatchPerformance performance;
		{
			std::lock_guard lock(mutex_);
			if (batchReported_ || !batchStarted_) {
				return;
			}
			batchReported_ = true;
			performance = performanceLocked();
		}
		if (batchObserver_) {
			try {
				batchObserver_(performance);
			} catch (...) {
			}
		}
	}

	/// Current-batch statistics while the run is in flight; `finishBatch()`
	/// reports the same fields once more at the end. Returns nullopt before
	/// the first frame arrives.
	[[nodiscard]] std::optional<BatchPerformance> livePerformanceSnapshot()
		const noexcept {
		std::lock_guard lock(mutex_);
		if (!batchStarted_) {
			return std::nullopt;
		}
		return performanceLocked();
	}

private:
	/// Caller must hold mutex_.
	[[nodiscard]] BatchPerformance performanceLocked() const {
		BatchPerformance performance;
		performance.completed = batchCompleted_;
		performance.failed = batchFailed_;
		performance.totalMilliseconds = elapsedMilliseconds(
			*batchStarted_, Clock::now());
		performance.framesPerSecond = performance.totalMilliseconds > 0.0
			? static_cast<double>(performance.completed) * 1000.0 /
				performance.totalMilliseconds
			: 0.0;
		auto sorted = batchStageDurations_;
		std::ranges::sort(sorted);
		performance.stageP50Milliseconds = percentile(sorted, 0.50);
		performance.stageP95Milliseconds = percentile(sorted, 0.95);
		performance.stageP99Milliseconds = percentile(sorted, 0.99);
		return performance;
	}

	[[nodiscard]] static double percentile(
		const std::vector<double>& sortedValues,
		double quantile) noexcept {
		if (sortedValues.empty()) {
			return 0.0;
		}
		const auto rank = static_cast<std::size_t>(
			std::ceil(quantile * static_cast<double>(sortedValues.size())));
		return sortedValues[std::max<std::size_t>(rank, 1) - 1];
	}

	struct TimingState {
		std::uint64_t sequenceNumber;
		Clock::time_point pipelineStarted;
		PipelineDurations durations;
	};

	void updateDuration(
		std::uint64_t executionId,
		double PipelineDurations::* member,
		Clock::time_point started) {
		std::lock_guard lock(mutex_);
		auto timing = timings_.find(executionId);
		if (timing != timings_.end()) {
			timing->second.durations.*member = elapsedMilliseconds(started, Clock::now());
		}
	}

	pipeline::Pipeline<ResultType> pipeline_;
	PipelineTimingObserver<ResultType> observer_;
	BatchPerformanceObserver batchObserver_;
	mutable std::mutex mutex_;
	std::unordered_map<std::uint64_t, TimingState> timings_;
	std::optional<Clock::time_point> batchStarted_;
	std::size_t batchCompleted_ = 0;
	std::size_t batchFailed_ = 0;
	std::vector<double> batchStageDurations_;
	bool batchReported_ = false;
};

} // namespace visionRuntime::benchmark