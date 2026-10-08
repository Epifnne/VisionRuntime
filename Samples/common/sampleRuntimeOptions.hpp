#pragma once

#include <visionruntime>
#include <nlohmann/json.hpp>

namespace visionSample {

struct PipelineOptions {
	float threshold = 2.0F;
	std::size_t resizeShortSide = 256;
	std::size_t cropWidth = 224;
	std::size_t cropHeight = 224;
	std::vector<float> mean{0.449F};
	std::vector<float> standardDeviation{0.226F};
	std::size_t maxBatchSize = 4;
	std::chrono::milliseconds flushTimeout{20};
	int backendStreams = 1;
	bool dynamicBatch = false;
	std::size_t imageChannels = 1;
};

struct RuntimeOptions {
	std::vector<visionRuntime::camera::FrameSourceConfig> sources;
	std::filesystem::path modelPackage;
	visionRuntime::config::DeploymentConfig deployment;
	PipelineOptions pipeline;
	visionRuntime::benchmark::TimingOutputPath timingOutput =
		visionRuntime::benchmark::TimingOutputPath::standardOutput();
	visionRuntime::executor::FaultPolicy faultPolicy =
		visionRuntime::executor::FaultPolicy::ReportAndContinue;
	std::string productDocumentJson;
};

inline visionRuntime::core::Result<void> validateAnomalyPreset(const RuntimeOptions& options) {
	if (options.pipeline.resizeShortSide != 256 ||
		options.pipeline.cropWidth != 224 || options.pipeline.cropHeight != 224 ||
		options.pipeline.imageChannels != 1 ||
		options.pipeline.mean != std::vector<float>{0.449F} ||
		options.pipeline.standardDeviation != std::vector<float>{0.226F}) {
		auto status = visionRuntime::core::Status::error(
			visionRuntime::core::StatusCode::Unsupported,
			"this sample uses AnomalyPreset: resize=256, crop=224x224, channels=1, "
			"mean=[0.449], standardDeviation=[0.226]; customize the sample to change these");
		visionRuntime::logs::report(status);
		return visionRuntime::core::Result<void>::failure(std::move(status));
	}
	return visionRuntime::core::Result<void>::success();
}

template<typename T>
visionRuntime::core::Result<T> fail(std::string message) {
	auto status = visionRuntime::core::Status::error(
		visionRuntime::core::StatusCode::InvalidArgument, std::move(message));
	visionRuntime::logs::report(status);
	return visionRuntime::core::Result<T>::failure(std::move(status));
}

inline visionRuntime::core::Result<std::vector<
	std::unique_ptr<visionRuntime::camera::IFrameSource>>> createSources(
	const RuntimeOptions& options) {
	using namespace visionRuntime;
	using Sources = std::vector<std::unique_ptr<camera::IFrameSource>>;
	if (options.sources.empty()) {
		return fail<Sources>("sample runtime requires at least one source");
	}
	Sources sources;
	for (const auto& config : options.sources) {
		auto source = camera::FrameSourceFactory::create(config);
		if (!source) {
			return core::Result<Sources>::failure(source.status());
		}
		sources.push_back(std::move(source).value());
	}
	return core::Result<Sources>::success(std::move(sources));
}

inline visionRuntime::core::Result<
	visionRuntime::pipeline::Pipeline<visionRuntime::vision::AnomalyResult>>
createPipeline(const RuntimeOptions& options, bool tuneBackend = false) {
	using namespace visionRuntime;
	using Pipeline = pipeline::Pipeline<vision::AnomalyResult>;
	auto package = config::ModelPackageLoader::load(options.modelPackage);
	if (!package) {
		return core::Result<Pipeline>::failure(package.status());
	}
	const auto& manifest = package->manifest();
	if (manifest.inputs.size() != 1 || manifest.outputs.size() != 1) {
		return fail<Pipeline>("sample requires one model input and one output");
	}
	auto preprocessor = preprocess::PreprocessBuilder::start<vision::Frame>()
		.then(preprocess::CvResize::shortSide(options.pipeline.resizeShortSide))
		.then(preprocess::CvCenterCrop({options.pipeline.cropWidth, options.pipeline.cropHeight}))
		.then(preprocess::ToTensor({
			.tensorName = manifest.inputs.front().name,
			.bufferCount = options.deployment.executor.stageQueueCapacity + 2,
			.channels = options.pipeline.imageChannels}))
		.then(preprocess::Normalize({
			.mean = options.pipeline.mean,
			.standardDeviation = options.pipeline.standardDeviation}))
		.build();
	if (!preprocessor) {
		return core::Result<Pipeline>::failure(preprocessor.status());
	}
	auto selected = package->selectArtifact(options.deployment.backend);
	if (!selected) {
		return core::Result<Pipeline>::failure(selected.status());
	}
	auto backendOptions = selected->optionsJson;
	if (tuneBackend && options.deployment.backend.id == "tensorrt") {
		try {
			auto json = nlohmann::json::parse(backendOptions);
			json["streams"] = options.pipeline.backendStreams;
			json["dynamicBatch"] = options.pipeline.dynamicBatch;
			backendOptions = json.dump();
		} catch (const nlohmann::json::exception& exception) {
			return fail<Pipeline>(std::string("backend options: ") + exception.what());
		}
	}
	auto backend = backends::PluginInferenceBackend::create({
		.pluginPath = backends::backendPluginPath(options.deployment.backend.pluginDirectory,
			options.deployment.backend.id),
		.backendId = options.deployment.backend.id,
		.artifactPath = selected->artifactPath,
		.device = selected->device,
		.optionsJson = std::move(backendOptions),
		.artifactKind = selected->artifactKind,
		.inputName = manifest.inputs.front().name,
		.outputName = manifest.outputs.front().name});
	if (!backend) {
		return core::Result<Pipeline>::failure(backend.status());
	}
	auto postprocessor = postprocess::AnomalyThresholdPostprocessor::create({
		.outputName = manifest.outputs.front().name,
		.threshold = options.pipeline.threshold});
	if (!postprocessor) {
		return core::Result<Pipeline>::failure(postprocessor.status());
	}
	return pipeline::PipelineBuilder<vision::AnomalyResult>{}
		.setPreprocessor(std::move(preprocessor).value())
		.setBackend(std::move(backend).value())
		.setPostprocessor(std::move(postprocessor).value())
		.build();
}

} // namespace visionSample
