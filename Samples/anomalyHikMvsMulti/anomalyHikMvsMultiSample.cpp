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

	// Build one Hik MVS continuous source per camera IP on the command line.
	std::vector<std::unique_ptr<camera::IFrameSource>> sources;
	for (int index = 3; index < argc; ++index) {
		auto sourceResult = camera::FrameSourceFactory::create(
			camera::ContinuousCameraSourceConfig{
				.device = {
					.ipAddress = argv[index],
					.pixelFormat = vision::PixelFormat::Gray8,
				},
			});
		if (!sourceResult) {
			return 1;
		}
		sources.push_back(std::move(sourceResult).value());
	}

	auto package = config::ModelPackageLoader::load(argv[1]);
	if (!package) {
		return 1;
	}
	const auto& manifest = package->manifest();

	preprocess::ToTensorOptions toTensorOptions{
		.tensorName = manifest.inputs.front().name,
		.bufferCount = deployment->executor.stageQueueCapacity + 2,
		.channels = 1,
	};
	preprocess::NormalizeOptions normalizeOptions{
		.mean = {0.449F},
		.standardDeviation = {0.226F},
	};
	auto preprocessor = preprocess::PreprocessBuilder::start<vision::Frame>()
		.then(preprocess::CvResize::shortSide(256))
		.then(preprocess::CvCenterCrop({224, 224}))
		.then(preprocess::ToTensor(std::move(toTensorOptions)))
		.then(preprocess::Normalize(std::move(normalizeOptions)))
		.build();
	if (!preprocessor) {
		return 1;
	}

	auto selected = package->selectArtifact(deployment->backend);
	if (!selected) {
		return 1;
	}
	auto backend = backends::PluginInferenceBackend::create({
		.pluginPath = backends::backendPluginPath(
			deployment->backend.pluginDirectory, deployment->backend.id),
		.backendId = deployment->backend.id,
		.artifactPath = selected->artifactPath,
		.device = selected->device,
		.optionsJson = selected->optionsJson,
		.artifactKind = selected->artifactKind,
		.inputName = manifest.inputs.front().name,
		.outputName = manifest.outputs.front().name,
	});
	if (!backend) {
		return 1;
	}

	constexpr float kThreshold = 2.0F;
	auto postprocessor = postprocess::AnomalyThresholdPostprocessor::create({
		.outputName = manifest.outputs.front().name,
		.threshold = kThreshold,
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
	std::unique_ptr<pipeline::IStagedVisionPipeline<vision::AnomalyResult>>
		stagedPipeline = std::make_unique<
			pipeline::Pipeline<vision::AnomalyResult>>(
				std::move(builtPipeline).value());

	const std::size_t cameraCount = sources.size();
	auto executor = runtime::RuntimeFactory::createBatchExecutor<
		vision::AnomalyResult>(
		std::move(stagedPipeline), *deployment,
		executor::BatchInferenceOptions{
			.maxBatchSize = cameraCount,
			.flushTimeout = std::chrono::milliseconds(20),
		});
	if (!executor) {
		return 1;
	}

	runtime::MultiCameraExecutionOptions<vision::AnomalyResult> options;
	options.duration = std::chrono::seconds(30);
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
		std::cerr << "camera " << index
			<< ": received=" << summary.receivedPerSource[index]
			<< ", dropped=" << summary.droppedPerSource[index] << '\n';
	}
}
