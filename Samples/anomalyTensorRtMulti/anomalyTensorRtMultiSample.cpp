// Multi-camera anomaly inspection on TensorRT, in the spirit of
// anomalyHikMvsSample: the pipeline is assembled by hand (source factory +
// preprocess chain + plugin backend + threshold postprocess +
// createBatchExecutor + MultiCameraSession), while deployment.json selects
// the backend/device/executor policy and profile.json selects the product
// parameters (preprocess shape, threshold, batch size, stream layout).
//
// Sources are Hik MVS cameras when camera IPs are passed, or directory
// replay (comma-separated directories) for camera-less validation. The
// TensorRT plugin runs a dynamic-batch engine; "streams" in profile.json
// selects how many execution contexts / CUDA streams the plugin runs in
// parallel.

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
	execution.duration = std::chrono::seconds(20);
	execution.faultPolicy = options.faultPolicy;
	return core::Result<std::unique_ptr<Session>>::success(std::make_unique<Session>(
		std::move(sources).value(), std::move(executor).value(), std::move(execution)));
}
} // namespace visionSample

#ifndef VISION_SAMPLE_SHELL
namespace {

using SampleProfile = visionSample::PipelineOptions;

void printUsage(const char* program) {
	std::cerr << "usage: " << program
		<< " <model-package> <deployment.json> <profile.json>"
		<< " <camera-ip>[,<camera-ip>...] | --dirs <dir>[,<dir>...]\n";
}

[[nodiscard]] nlohmann::json loadJson(const std::filesystem::path& path) {
	std::ifstream stream(path);
	if (!stream) {
		throw std::runtime_error("cannot open " + path.string());
	}
	return nlohmann::json::parse(stream);
}

[[nodiscard]] SampleProfile loadProfile(const std::filesystem::path& path) {
	const auto document = loadJson(path);
	const auto& pipeline = document.at("pipeline");
	SampleProfile profile;
	profile.threshold = pipeline.value("threshold", 2.0F);
	profile.resizeShortSide =
		pipeline.value("resizeShortSide", std::size_t{224});
	profile.cropWidth = pipeline.value("cropWidth", std::size_t{224});
	profile.cropHeight = pipeline.value("cropHeight", std::size_t{224});
	profile.mean = pipeline.value("mean", std::vector<float>{0.449F});
	profile.standardDeviation =
		pipeline.value("standardDeviation", std::vector<float>{0.226F});
	profile.maxBatchSize = pipeline.value("maxBatchSize", std::size_t{4});
	profile.flushTimeout = std::chrono::milliseconds(
		pipeline.value("flushTimeoutMilliseconds", 10));
	profile.imageChannels = document.value("channels", std::size_t{1});
	const auto& backend = document.at("backend");
	profile.backendStreams = backend.value("streams", 1);
	profile.dynamicBatch = backend.value("dynamicBatch", false);
	return profile;
}

[[nodiscard]] std::vector<std::string> splitList(std::string text) {
	std::vector<std::string> result;
	std::size_t begin = 0;
	for (;;) {
		const auto comma = text.find(',', begin);
		auto token = text.substr(begin, comma - begin);
		if (!token.empty()) {
			result.push_back(std::move(token));
		}
		if (comma == std::string::npos) {
			break;
		}
		begin = comma + 1;
	}
	return result;
}

} // namespace

int main(int argc, char* argv[]) {
	if (argc < 5) {
		printUsage(argv[0]);
		return 1;
	}

	using namespace visionRuntime;
	SampleProfile profile;
	config::DeploymentConfig deployment;
	try {
		profile = loadProfile(argv[3]);
	} catch (const std::exception& exception) {
		logs::error(std::string("profile.json: ") + exception.what());
		return 1;
	}
	auto deploymentResult = config::ConfigLoader::loadDeployment(argv[2]);
	if (!deploymentResult) {
		return 1;
	}
	deployment = std::move(deploymentResult).value();

	visionSample::RuntimeOptions options;
	options.modelPackage = argv[1];
	options.deployment = std::move(deployment);
	options.pipeline = profile;
	if (std::string(argv[4]) == "--dirs") {
		if (argc < 6) {
			printUsage(argv[0]);
			return 1;
		}
		for (const auto& directory : splitList(argv[5])) {
			options.sources.push_back(
				camera::FileFrameSourceConfig{
					.source = {
						.directory = directory,
						.extensions = {".bmp"},
						.frameInterval = std::chrono::milliseconds{0},
						.loop = true,
					},
				});
		}
	} else {
		for (int index = 4; index < argc; ++index) {
			options.sources.push_back(
				camera::ContinuousCameraSourceConfig{
					.device = {
						.ipAddress = argv[index],
						.pixelFormat = vision::PixelFormat::Gray8,
					},
				});
		}
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
		std::cerr << "source " << index
			<< ": received=" << summary.receivedPerSource[index]
			<< ", dropped=" << summary.droppedPerSource[index] << '\n';
	}
	return summary.failed == 0 && summary.sourceFailures == 0 ? 0 : 2;
}
#endif
