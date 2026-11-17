// Multi-camera anomaly inspection: several Hik MVS cameras feed one shared
// model through a BatchPipelineExecutor, which aggregates frames into a batch
// tensor and runs a single inference per batch.

#include <visionruntime>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
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
	auto pipeline = createPipeline(options);
	if (!pipeline) {
		return core::Result<std::unique_ptr<Session>>::failure(pipeline.status());
	}
	auto executor = runtime::RuntimeFactory::createBatchExecutor<vision::AnomalyResult>(
		std::make_unique<pipeline::Pipeline<vision::AnomalyResult>>(std::move(pipeline).value()),
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
