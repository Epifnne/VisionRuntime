// Multi-camera anomaly inspection on TensorRT, assembled by hand (source factory +
// preprocess chain + plugin backend + threshold postprocess +
// createBatchExecutor + MultiCameraSession). The shared product profile
// selects sources, model/backend parameters and UI for both CLI and Shell.

#include <visionruntime>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "sampleRuntime.hpp"

namespace visionSample {
using namespace visionRuntime;

core::Result<std::unique_ptr<runtime::MultiCameraSession<vision::AnomalyResult>>>
createSampleRuntime(RuntimeOptions options) {
	using Session = runtime::MultiCameraSession<vision::AnomalyResult>;
	auto sources = createSources(options);
	if (!sources) {
		return core::Result<std::unique_ptr<Session>>::failure(sources.status());
	}
	auto pipeline = createPipeline(options, true);
	if (!pipeline) {
		return core::Result<std::unique_ptr<Session>>::failure(pipeline.status());
	}
	auto printed = std::make_shared<std::atomic_bool>(false);
	benchmark::PipelineTimingObserver<vision::AnomalyResult> observer =
		[printed](std::uint64_t, const core::Result<vision::AnomalyResult>& result,
			const benchmark::PipelineDurations&) {
			if (result && !printed->exchange(true)) {
				std::cerr << "score=" << result->score << ", threshold=" << result->threshold
					<< ", decision=" << vision::anomalyDecisionName(result->decision) << '\n';
			}
		};
	auto executor = runtime::RuntimeFactory::createBatchExecutor<vision::AnomalyResult>(
		std::make_unique<benchmark::TimedPipeline<vision::AnomalyResult>>(
			std::move(pipeline).value(), std::move(observer)),
		options.deployment, {.maxBatchSize = options.pipeline.maxBatchSize,
			.flushTimeout = options.pipeline.flushTimeout});
	if (!executor) {
		return core::Result<std::unique_ptr<Session>>::failure(executor.status());
	}
	runtime::MultiCameraExecutionOptions<vision::AnomalyResult> execution;
	execution.duration = options.duration;
	execution.faultPolicy = options.faultPolicy;
	return core::Result<std::unique_ptr<Session>>::success(std::make_unique<Session>(
		std::move(sources).value(), std::move(executor).value(), std::move(execution)));
}
} // namespace visionSample
