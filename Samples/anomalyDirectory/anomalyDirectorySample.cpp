#include <visionruntime>

#include <filesystem>
#include <iostream>
#include "sampleRuntime.hpp"

namespace visionSample {
using namespace visionRuntime;

core::Result<std::unique_ptr<visionService::session::IInspectionSession>>
createSampleRuntime(RuntimeOptions options) {
	using Session = std::unique_ptr<visionService::session::IInspectionSession>;
	auto sources = createSources(options);
	if (!sources) {
		return core::Result<Session>::failure(sources.status());
	}
	auto pipeline = createPipeline(options, true);
	if (!pipeline) {
		return core::Result<Session>::failure(pipeline.status());
	}
	if (sources->size() > 1 || options.pipeline.maxBatchSize > 1) {
		auto executor = runtime::RuntimeFactory::createBatchExecutor<vision::AnomalyResult>(
			std::make_unique<pipeline::Pipeline<vision::AnomalyResult>>(std::move(pipeline).value()),
			options.deployment, {.maxBatchSize = options.pipeline.maxBatchSize,
				.flushTimeout = options.pipeline.flushTimeout});
		if (!executor) {
			return core::Result<Session>::failure(executor.status());
		}
		runtime::MultiCameraExecutionOptions<vision::AnomalyResult> execution;
		execution.duration = options.duration;
		execution.faultPolicy = options.faultPolicy;
		return core::Result<Session>::success(visionService::session::adoptRuntime(
			std::make_unique<runtime::MultiCameraSession<vision::AnomalyResult>>(
				std::move(sources).value(), std::move(executor).value(), std::move(execution))));
	}
	auto runtime = runtime::RuntimeFactory::createRuntime<vision::AnomalyResult>(
		std::move(sources->front()),
		std::make_unique<pipeline::Pipeline<vision::AnomalyResult>>(std::move(pipeline).value()),
		options.deployment, {.duration = options.duration, .faultPolicy = options.faultPolicy});
	if (!runtime) {
		return core::Result<Session>::failure(runtime.status());
	}
	return core::Result<Session>::success(
		visionService::session::adoptRuntime(std::move(runtime).value()));
}
} // namespace visionSample
