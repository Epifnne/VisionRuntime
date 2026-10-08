#include <visionruntime>

#include <atomic>
#include <filesystem>
#include <iostream>
#include <utility>
#include "sampleRuntime.hpp"

namespace visionSample {
using namespace visionRuntime;

core::Result<std::unique_ptr<runtime::RuntimeSession<vision::AnomalyResult>>>
createSampleRuntime(RuntimeOptions options) {
	if (options.sources.size() != 1) {
		return fail<std::unique_ptr<runtime::RuntimeSession<vision::AnomalyResult>>>(
			"anomalyHikMvs requires exactly one source");
	}
	auto firstResultPrinted = std::make_shared<std::atomic_bool>(false);
	auto supported = validateAnomalyPreset(options);
	if (!supported) {
		return core::Result<std::unique_ptr<runtime::RuntimeSession<vision::AnomalyResult>>>::failure(
			supported.status());
	}
	return runtime::RuntimeFactory::createFromPreset<runtime::presets::AnomalyPreset>({
		.source = std::move(options.sources.front()),
		.model = {.packagePath = options.modelPackage},
		.threshold = options.pipeline.threshold,
		.deployment = std::move(options.deployment),
		.callback = [firstResultPrinted](executor::TaskId,
			const core::Result<vision::AnomalyResult>& result) {
			if (!result || firstResultPrinted->exchange(true)) {
				return;
			}
			std::cerr << "score=" << result->score << ", threshold=" << result->threshold
				<< ", decision=" << vision::anomalyDecisionName(result->decision) << '\n';
		},
		.execution = {.faultPolicy = options.faultPolicy},
	});
}
} // namespace visionSample

#ifndef VISION_SAMPLE_SHELL
int main(int argc, char* argv[]) {
	if (argc != 3) {
		std::cerr << "usage: anomalyHikMvsSample <model-package> <deployment.json>\n";
		return 1;
	}

	using namespace visionRuntime;
	auto deployment = config::ConfigLoader::loadDeployment(argv[2]);
	if (!deployment) {
		return 1;
	}
	visionSample::RuntimeOptions options;
	options.sources.push_back(camera::ContinuousCameraSourceConfig{
		.device = {.ipAddress = "169.254.239.231", .pixelFormat = vision::PixelFormat::Gray8}});
	options.modelPackage = argv[1];
	options.deployment = std::move(deployment).value();
	auto sessionResult = visionSample::createSampleRuntime(std::move(options));
	if (!sessionResult) {
		return 1;
	}
	auto session = std::move(sessionResult).value();
	auto startResult = session->start();
	if (!startResult) {
		return 1;
	}
	const auto summary = session->wait();
	std::cerr << "received=" << summary.received
		<< ", submitted=" << summary.submitted
		<< ", completed=" << summary.completed
		<< ", failed=" << summary.failed
		<< ", dropped=" << summary.dropped
		<< ", sourceFailures=" << summary.sourceFailures << '\n';
}
#endif
