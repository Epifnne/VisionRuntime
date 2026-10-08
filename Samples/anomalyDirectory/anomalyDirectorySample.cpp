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
	auto supported = validateAnomalyPreset(options);
	if (!supported) {
		return core::Result<std::unique_ptr<runtime::RuntimeSession<vision::AnomalyResult>>>::failure(
			supported.status());
	}
	return runtime::RuntimeFactory::createFromPreset<runtime::presets::AnomalyPreset>({
		.source = std::move(options.sources.front()),
		.model = {.packagePath = options.modelPackage},
		.threshold = options.pipeline.threshold,
		.timed = true,
		.timingOutput = std::move(options.timingOutput),
		.deployment = std::move(options.deployment),
		.callback = [](executor::TaskId, const core::Result<vision::AnomalyResult>&) {},
		.execution = {.faultPolicy = options.faultPolicy},
	});
}
} // namespace visionSample

#ifndef VISION_SAMPLE_SHELL
int main(int argc, char* argv[]) {
	if (argc != 4) {
		std::cerr << "usage: anomalyDirectorySample <benchmark.csv> <model-package> "
			"<deployment.json>\n";
		return 1;
	}

	using namespace visionRuntime;
	auto deployment = config::ConfigLoader::loadDeployment(argv[3]);
	if (!deployment) {
		return 1;
	}
	visionSample::RuntimeOptions options;
	options.sources.push_back(camera::FileFrameSourceConfig{.source = {.directory = "image"}});
	options.modelPackage = argv[2];
	options.deployment = std::move(deployment).value();
	options.timingOutput = benchmark::TimingOutputPath::file(argv[1]);
	auto sessionResult = visionSample::createSampleRuntime(std::move(options));
	if (!sessionResult) {
		return 1;
	}
	auto session = std::move(sessionResult).value();
	auto startResult = session->start();
	if (!startResult) {
		return 1;
	}
	static_cast<void>(session->wait());
}
#endif
