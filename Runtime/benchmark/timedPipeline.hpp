#pragma once

#include "benchmark/time.hpp"
#include "core/result.hpp"
#include "pipeline/pipeline.hpp"

#include <cstdint>
#include <functional>
#include <utility>

namespace visionRuntime::benchmark {

template<typename ResultType>
using PipelineTimingObserver = std::function<void(
	std::uint64_t sequenceNumber,
	const core::Result<ResultType>& result,
	const PipelineDurations& durations)>;

template<typename ResultType>
class TimedPipeline final : public pipeline::IVisionPipeline<ResultType> {
public:
	TimedPipeline(
		pipeline::Pipeline<ResultType> pipeline,
		PipelineTimingObserver<ResultType> observer)
		: pipeline_(std::move(pipeline)), observer_(std::move(observer)) {}

	[[nodiscard]] core::Result<ResultType> run(
		pipeline::PipelinePacket packet) override {
		const auto* frame = packet.cameraFrame();
		const auto sequenceNumber = frame == nullptr
			? 0
			: frame->metadata().sequenceNumber;
		PipelineDurations durations;
		const auto pipelineStarted = Clock::now();

		const auto preprocessStarted = Clock::now();
		auto prepared = pipeline_.preprocess(std::move(packet));
		durations.preprocessMilliseconds = elapsedMilliseconds(
			preprocessStarted, Clock::now());
		if (!prepared) {
			return finish(core::Result<ResultType>::failure(prepared.status()),
				sequenceNumber, pipelineStarted, durations);
		}

		const auto inferenceStarted = Clock::now();
		auto output = pipeline_.infer(std::move(prepared).value());
		durations.inferenceMilliseconds = elapsedMilliseconds(
			inferenceStarted, Clock::now());
		if (!output) {
			return finish(core::Result<ResultType>::failure(output.status()),
				sequenceNumber, pipelineStarted, durations);
		}

		const auto postprocessStarted = Clock::now();
		auto result = pipeline_.postprocess(std::move(output).value());
		durations.postprocessMilliseconds = elapsedMilliseconds(
			postprocessStarted, Clock::now());
		return finish(std::move(result), sequenceNumber, pipelineStarted, durations);
	}

private:
	[[nodiscard]] core::Result<ResultType> finish(
		core::Result<ResultType> result,
		std::uint64_t sequenceNumber,
		Clock::time_point pipelineStarted,
		PipelineDurations& durations) {
		durations.totalMilliseconds = elapsedMilliseconds(
			pipelineStarted, Clock::now());
		if (observer_) {
			observer_(sequenceNumber, result, durations);
		}
		return result;
	}

	pipeline::Pipeline<ResultType> pipeline_;
	PipelineTimingObserver<ResultType> observer_;
};

} // namespace visionRuntime::benchmark