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
	execution.duration = std::chrono::seconds(30);
	execution.faultPolicy = options.faultPolicy;
	return core::Result<std::unique_ptr<Session>>::success(std::make_unique<Session>(
		std::move(sources).value(), std::move(executor).value(), std::move(execution)));
}
} // namespace visionSample

#ifndef VISION_SAMPLE_SHELL
namespace {

void printUsage(const char* program) {
	std::cerr << "usage: " << program
		<< " <model-package> <deployment.json> <camera-ip> [<camera-ip>...]\n";
}

} // namespace

int main(int argc, char* argv[]) {
	if (argc < 4) {
		printUsage(argv[0]);
		return 1;
	}

	using namespace visionRuntime;
	auto deployment = config::ConfigLoader::loadDeployment(argv[2]);
	if (!deployment) {
		return 1;
	}

	visionSample::RuntimeOptions options;
	options.pipeline.maxBatchSize = static_cast<std::size_t>(argc - 3);
	options.modelPackage = argv[1];
	options.deployment = std::move(deployment).value();
	for (int index = 3; index < argc; ++index) {
		options.sources.push_back(
			camera::ContinuousCameraSourceConfig{
				.device = {
					.ipAddress = argv[index],
					.pixelFormat = vision::PixelFormat::Gray8,
				},
			});
	}

	auto session = visionSample::createSampleRuntime(std::move(options));
	if (!session) {
		return 1;
	}
	auto started = session.value()->start();
	if (!started) {
		return 1;
	}
	const auto summary = session.value()->wait();
	std::cerr << "received=" << summary.received
		<< ", submitted=" << summary.submitted
		<< ", completed=" << summary.completed
		<< ", failed=" << summary.failed
		<< ", dropped=" << summary.dropped
		<< ", sourceFailures=" << summary.sourceFailures << '\n';
	for (std::size_t index = 0; index < summary.receivedPerSource.size(); ++index) {
		std::cerr << "camera " << index
			<< ": received=" << summary.receivedPerSource[index]
			<< ", dropped=" << summary.droppedPerSource[index] << '\n';
	}
}
#endif
