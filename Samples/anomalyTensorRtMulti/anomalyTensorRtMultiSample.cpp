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

namespace {

struct SampleProfile {
	float threshold = 2.0F;
	std::size_t resizeShortSide = 224;
	std::size_t cropWidth = 224;
	std::size_t cropHeight = 224;
	std::vector<float> mean{0.449F};
	std::vector<float> standardDeviation{0.226F};
	std::size_t maxBatchSize = 4;
	std::chrono::milliseconds flushTimeout{10};
	int backendStreams = 1;
	bool dynamicBatch = false;
	std::size_t imageChannels = 1;
};

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

	// Build one source per camera IP or per replay directory.
	std::vector<std::unique_ptr<camera::IFrameSource>> sources;
	if (std::string(argv[4]) == "--dirs") {
		if (argc < 6) {
			printUsage(argv[0]);
			return 1;
		}
		for (const auto& directory : splitList(argv[5])) {
			auto source = camera::FrameSourceFactory::create(
				camera::FileFrameSourceConfig{
					.source = {
						.directory = directory,
						.extensions = {".bmp"},
						.frameInterval = std::chrono::milliseconds{0},
						.loop = true,
					},
				});
			if (!source) {
				return 1;
			}
			sources.push_back(std::move(source).value());
		}
	} else {
		for (int index = 4; index < argc; ++index) {
			auto source = camera::FrameSourceFactory::create(
				camera::ContinuousCameraSourceConfig{
					.device = {
						.ipAddress = argv[index],
						.pixelFormat = vision::PixelFormat::Gray8,
					},
				});
			if (!source) {
				return 1;
			}
			sources.push_back(std::move(source).value());
		}
	}

	auto package = config::ModelPackageLoader::load(argv[1]);
	if (!package) {
		return 1;
	}
	const auto& manifest = package->manifest();

	preprocess::ToTensorOptions toTensorOptions{
		.tensorName = manifest.inputs.front().name,
		.bufferCount = deployment.executor.stageQueueCapacity + 2,
		.channels = profile.imageChannels,
	};
	preprocess::NormalizeOptions normalizeOptions{
		.mean = profile.mean,
		.standardDeviation = profile.standardDeviation,
	};
	auto preprocessor = preprocess::PreprocessBuilder::start<vision::Frame>()
		.then(preprocess::CvResize::shortSide(profile.resizeShortSide))
		.then(preprocess::CvCenterCrop({profile.cropWidth, profile.cropHeight}))
		.then(preprocess::ToTensor(std::move(toTensorOptions)))
		.then(preprocess::Normalize(std::move(normalizeOptions)))
		.build();
	if (!preprocessor) {
		return 1;
	}

	auto selected = package->selectArtifact(deployment.backend);
	if (!selected) {
		return 1;
	}
	// Merge the sample's stream-parallelism choice into the artifact options
	// handed to the backend plugin.
	auto backendOptions = nlohmann::json::parse(selected->optionsJson);
	backendOptions["streams"] = profile.backendStreams;
	backendOptions["dynamicBatch"] = profile.dynamicBatch;
	auto backend = backends::PluginInferenceBackend::create({
		.pluginPath = backends::backendPluginPath(
			deployment.backend.pluginDirectory, deployment.backend.id),
		.backendId = deployment.backend.id,
		.artifactPath = selected->artifactPath,
		.device = selected->device,
		.optionsJson = backendOptions.dump(),
		.artifactKind = selected->artifactKind,
		.inputName = manifest.inputs.front().name,
		.outputName = manifest.outputs.front().name,
	});
	if (!backend) {
		return 1;
	}

	auto postprocessor = postprocess::AnomalyThresholdPostprocessor::create({
		.outputName = manifest.outputs.front().name,
		.threshold = profile.threshold,
	});
	if (!postprocessor) {
		return 1;
	}

	pipeline::PipelineBuilder<vision::AnomalyResult> builder;
	auto builtPipeline = builder
		.setPreprocessor(std::move(preprocessor).value())
		.setBackend(std::move(backend).value())
		.setPostprocessor(std::move(postprocessor).value())
		.build();
	if (!builtPipeline) {
		return 1;
	}

	std::atomic_bool firstResultPrinted = false;
	benchmark::PipelineTimingObserver<vision::AnomalyResult> timingObserver =
		[&firstResultPrinted](std::uint64_t,
			const core::Result<vision::AnomalyResult>& result,
			const benchmark::PipelineDurations&) {
			if (!result || firstResultPrinted.exchange(true)) {
				return;
			}
			std::cerr << "score=" << result->score
				<< ", threshold=" << result->threshold
				<< ", decision="
				<< vision::anomalyDecisionName(result->decision) << '\n';
		};
	std::unique_ptr<pipeline::IStagedVisionPipeline<vision::AnomalyResult>>
		stagedPipeline = std::make_unique<
			benchmark::TimedPipeline<vision::AnomalyResult>>(
				std::move(builtPipeline).value(), std::move(timingObserver));

	auto executor = runtime::RuntimeFactory::createBatchExecutor<
		vision::AnomalyResult>(
		std::move(stagedPipeline), deployment,
		executor::BatchInferenceOptions{
			.maxBatchSize = profile.maxBatchSize,
			.flushTimeout = profile.flushTimeout,
		});
	if (!executor) {
		return 1;
	}

	runtime::MultiCameraExecutionOptions<vision::AnomalyResult> options;
	options.duration = std::chrono::seconds(20);
	runtime::MultiCameraSession<vision::AnomalyResult> session(
		std::move(sources), std::move(executor).value(), std::move(options));

	auto started = session.start();
	if (!started) {
		return 1;
	}
	const auto summary = session.wait();
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
