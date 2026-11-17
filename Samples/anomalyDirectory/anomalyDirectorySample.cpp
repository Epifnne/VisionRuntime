#include <visionruntime>

#include <filesystem>
#include <iostream>
#include "sampleRuntime.hpp"

namespace visionSample {
using namespace visionRuntime;

core::Result<std::unique_ptr<runtime::RuntimeSession<vision::AnomalyResult>>>
createSampleRuntime(RuntimeOptions options) {
	if (options.sources.size() != 1) {
		return fail<std::unique_ptr<runtime::RuntimeSession<vision::AnomalyResult>>>(
			"anomalyDirectory requires exactly one source");
	}
	auto sources = createSources(options);
	if (!sources) {
		return core::Result<std::unique_ptr<runtime::RuntimeSession<vision::AnomalyResult>>>::failure(
			sources.status());
	}
	auto pipeline = createPipeline(options);
	if (!pipeline) {
		return core::Result<std::unique_ptr<runtime::RuntimeSession<vision::AnomalyResult>>>::failure(
			pipeline.status());
	}
	return runtime::RuntimeFactory::createRuntime<vision::AnomalyResult>(
		std::move(sources->front()),
		std::make_unique<pipeline::Pipeline<vision::AnomalyResult>>(std::move(pipeline).value()),
		options.deployment, {.duration = options.duration, .faultPolicy = options.faultPolicy});
}
} // namespace visionSample
