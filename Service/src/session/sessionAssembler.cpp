#include "session/sessionAssembler.hpp"

#include "backends/pluginInferenceBackend.hpp"
#include "benchmark/timedPipeline.hpp"
#include "camera/cameraSourceOptions.hpp"
#include "camera/continuousCameraSource.hpp"
#include "camera/fileSource.hpp"
#include "camera/hikrobotMvsCameraDevice.hpp"
#include "camera/timedTriggerSource.hpp"
#include "config/buildProfile.hpp"
#include "config/deploymentConfig.hpp"
#include "config/modelPackageLoader.hpp"
#include "executor/batchPipelineExecutor.hpp"
#include "pipeline/pipelineBuilder.hpp"
#include "postprocess/anomalyThresholdPostprocessor.hpp"
#include "preprocess/frameNodes/cvCenterCropNode.hpp"
#include "preprocess/frameNodes/cvResizeNode.hpp"
#include "preprocess/frameNodes/toTensorNode.hpp"
#include "preprocess/preprocessChain.hpp"
#include "preprocess/tensorNodes/normalizeNode.hpp"
#include "runtime/runtimeFactory.hpp"
#include "vision/frame.hpp"

#include <chrono>
#include <utility>

namespace visionService::session {

namespace {

namespace backends = visionRuntime::backends;
namespace benchmark = visionRuntime::benchmark;
namespace config = visionRuntime::config;
namespace executor = visionRuntime::executor;
namespace pipeline = visionRuntime::pipeline;
namespace postprocess = visionRuntime::postprocess;
namespace preprocess = visionRuntime::preprocess;
namespace vision = visionRuntime::vision;

template<typename T>
[[nodiscard]] core::Result<T> failure(
	core::StatusCode code, std::string message) {
	return core::Result<T>::failure(
		core::Status::error(code, std::move(message)));
}

[[nodiscard]] core::Result<vision::PixelFormat> parsePixelFormat(
	const std::string& text) {
	if (text == "gray8") {
		return core::Result<vision::PixelFormat>::success(vision::PixelFormat::Gray8);
	}
	if (text == "gray16") {
		return core::Result<vision::PixelFormat>::success(
			vision::PixelFormat::Gray16);
	}
	if (text == "bgr8") {
		return core::Result<vision::PixelFormat>::success(vision::PixelFormat::Bgr8);
	}
	if (text == "rgb8") {
		return core::Result<vision::PixelFormat>::success(vision::PixelFormat::Rgb8);
	}
	if (text == "bgra8") {
		return core::Result<vision::PixelFormat>::success(vision::PixelFormat::Bgra8);
	}
	if (text == "rgba8") {
		return core::Result<vision::PixelFormat>::success(vision::PixelFormat::Rgba8);
	}
	return failure<vision::PixelFormat>(core::StatusCode::InvalidArgument,
		"unsupported pixel format: " + text);
}

[[nodiscard]] core::Result<std::unique_ptr<camera::ICameraDevice>>
createCameraDevice(camera::CameraDeviceOptions options) {
	if constexpr (config::BuildProfile::cameraSdk ==
		config::CameraSdk::HikMvs) {
		auto device =
			camera::HikrobotMvsCameraDevice::create(std::move(options));
		if (!device) {
			return core::Result<std::unique_ptr<camera::ICameraDevice>>::failure(
				device.status());
		}
		return core::Result<std::unique_ptr<camera::ICameraDevice>>::success(
			std::move(device).value());
	} else {
		return failure<std::unique_ptr<camera::ICameraDevice>>(
			core::StatusCode::Unsupported,
			"this build has no camera SDK; camera sources are unavailable");
	}
}

struct SourceBuild {
	std::unique_ptr<camera::IFrameSource> source;
	camera::ICameraDevice* device = nullptr;
};

[[nodiscard]] core::Result<SourceBuild> buildDirectorySource(
	const profile::SourceProfile& sourceProfile) {
	camera::FileSourceOptions options;
	options.directory = sourceProfile.directory.directory;
	options.extensions = sourceProfile.directory.extensions;
	options.loop = sourceProfile.directory.loop;
	options.recursive = sourceProfile.directory.recursive;
	options.frameInterval = sourceProfile.directory.frameInterval;
	auto source = camera::FileSource::create(std::move(options));
	if (!source) {
		return failure<SourceBuild>(source.status().code(),
			std::string(source.status().message()));
	}
	return core::Result<SourceBuild>::success(
		SourceBuild{std::move(source).value(), nullptr});
}

[[nodiscard]] core::Result<SourceBuild> buildCameraSource(
	const profile::SourceProfile& sourceProfile) {
	const auto& cameraProfile = sourceProfile.camera;
	auto pixelFormat = parsePixelFormat(cameraProfile.pixelFormat);
	if (!pixelFormat) {
		return failure<SourceBuild>(pixelFormat.status().code(),
			std::string(pixelFormat.status().message()));
	}
	camera::CameraDeviceOptions deviceOptions;
	deviceOptions.serialNumber = cameraProfile.serialNumber;
	deviceOptions.ipAddress = cameraProfile.ipAddress;
	deviceOptions.pixelFormat = pixelFormat.value();
	deviceOptions.exposureMicroseconds = cameraProfile.exposureMicroseconds;
	deviceOptions.gain = cameraProfile.gain;
	deviceOptions.maxFramesInFlight = cameraProfile.maxFramesInFlight;

	auto device = createCameraDevice(std::move(deviceOptions));
	if (!device) {
		return failure<SourceBuild>(device.status().code(),
			std::string(device.status().message()));
	}
	auto* devicePointer = device.value().get();

	if (cameraProfile.mode == "timedTrigger") {
		camera::TimedTriggerSourceOptions options;
		options.triggerInterval = cameraProfile.triggerInterval;
		options.responseTimeout = cameraProfile.responseTimeout;
		auto source = camera::TimedTriggerSource::create(
			std::move(device).value(), options);
		if (!source) {
			return failure<SourceBuild>(source.status().code(),
				std::string(source.status().message()));
		}
		return core::Result<SourceBuild>::success(
			SourceBuild{std::move(source).value(), devicePointer});
	}

	camera::ContinuousCameraSourceOptions options;
	options.frameRate = cameraProfile.frameRate;
	auto source = camera::ContinuousCameraSource::create(
		std::move(device).value(), options);
	if (!source) {
		return failure<SourceBuild>(source.status().code(),
			std::string(source.status().message()));
	}
	return core::Result<SourceBuild>::success(
		SourceBuild{std::move(source).value(), devicePointer});
}

[[nodiscard]] core::Result<config::DeploymentConfig> buildDeployment(
	const profile::ProductProfile& profile) {
	config::DeploymentConfig deployment;
	deployment.backend.pluginDirectory = profile.model.pluginDirectory;
	deployment.backend.id = profile.model.backendId;
	deployment.backend.device = profile.model.device;
	deployment.executor.queueCapacity = profile.pipeline.queueCapacity;
	deployment.executor.stageQueueCapacity =
		profile.pipeline.stageQueueCapacity;
	if (profile.pipeline.queueFullPolicy == "block") {
		deployment.executor.queueFullPolicy = config::QueueFullPolicy::Block;
	} else if (profile.pipeline.queueFullPolicy == "drop") {
		deployment.executor.queueFullPolicy = config::QueueFullPolicy::Drop;
	} else {
		return failure<config::DeploymentConfig>(core::StatusCode::InvalidArgument,
			"pipeline.queueFullPolicy must be drop or block");
	}
	return core::Result<config::DeploymentConfig>::success(deployment);
}

} // namespace

core::Result<AssembledSession> SessionAssembler::assemble(
	const profile::ProductProfile& profile,
	endpoints::EndpointRegistry& registry,
	benchmark::PipelineTimingObserver<SessionResult> timingObserver,
	benchmark::BatchPerformanceObserver batchObserver) {
	AssembledSession assembled;
	assembled.sourceCount = profile.sources.size();
	assembled.cameraDevices.assign(profile.sources.size(), nullptr);

	std::vector<std::unique_ptr<camera::IFrameSource>> sources;
	sources.reserve(profile.sources.size());
	auto streamBindings = std::make_shared<
		std::vector<std::pair<std::uint32_t, std::string>>>();
	streamBindings->reserve(profile.sources.size());
	for (std::size_t index = 0; index < profile.sources.size(); ++index) {
		const auto& sourceProfile = profile.sources[index];
		const auto sourceId = static_cast<std::uint32_t>(index);
		const std::string streamName = "stream." + sourceProfile.id;
		auto registered = registry.registerStream({
			.name = streamName,
			.description = sourceProfile.role.empty()
				? "source frames"
				: sourceProfile.role,
			.accessLevel = endpoints::AccessLevel::Operator,
			.sourceId = sourceId,
		});
		if (!registered) {
			return failure<AssembledSession>(registered.status().code(),
				std::string(registered.status().message()));
		}

		auto built = sourceProfile.type == "directory"
			? buildDirectorySource(sourceProfile)
			: buildCameraSource(sourceProfile);
		if (!built) {
			return failure<AssembledSession>(built.status().code(),
				std::string(built.status().message()));
		}
		assembled.cameraDevices[index] = built->device;
		sources.push_back(std::move(built).value().source);
		streamBindings->emplace_back(sourceId, streamName);
	}

	auto package = config::ModelPackageLoader::load(profile.model.packagePath);
	if (!package) {
		return failure<AssembledSession>(package.status().code(),
			std::string(package.status().message()));
	}
	const auto& modelManifest = package->manifest();

	auto deployment = buildDeployment(profile);
	if (!deployment) {
		return failure<AssembledSession>(deployment.status().code(),
			std::string(deployment.status().message()));
	}

	const auto& inputTensor = modelManifest.inputs.front();
	if (inputTensor.layout != config::TensorLayout::Nchw ||
		inputTensor.shape.size() != 4) {
		return failure<AssembledSession>(core::StatusCode::Unsupported,
			"model input must be a ranked NCHW tensor");
	}
	preprocess::ToTensorOptions toTensorOptions{
		.tensorName = inputTensor.name,
		.bufferCount = deployment->executor.stageQueueCapacity + 2,
		.channels = inputTensor.shape[1],
	};
	preprocess::NormalizeOptions normalizeOptions{
		.mean = profile.pipeline.mean,
		.standardDeviation = profile.pipeline.standardDeviation,
	};
	auto preprocessor = preprocess::PreprocessBuilder::start<vision::Frame>()
		.then(preprocess::CvResize::shortSide(profile.pipeline.resizeShortSide))
		.then(preprocess::CvCenterCrop({
			profile.pipeline.cropWidth, profile.pipeline.cropHeight}))
		.then(preprocess::ToTensor(std::move(toTensorOptions)))
		.then(preprocess::Normalize(std::move(normalizeOptions)))
		.build();
	if (!preprocessor) {
		return failure<AssembledSession>(preprocessor.status().code(),
			std::string(preprocessor.status().message()));
	}

	auto selected = package->selectArtifact(deployment->backend);
	if (!selected) {
		return failure<AssembledSession>(selected.status().code(),
			std::string(selected.status().message()));
	}
	auto backend = backends::PluginInferenceBackend::create({
		.pluginPath = backends::backendPluginPath(
			deployment->backend.pluginDirectory, deployment->backend.id),
		.backendId = deployment->backend.id,
		.artifactPath = selected->artifactPath,
		.device = selected->device,
		.optionsJson = selected->optionsJson,
		.artifactKind = selected->artifactKind,
		.inputName = modelManifest.inputs.front().name,
		.outputName = modelManifest.outputs.front().name,
	});
	if (!backend) {
		return failure<AssembledSession>(backend.status().code(),
			std::string(backend.status().message()));
	}

	auto postprocessor = postprocess::AnomalyThresholdPostprocessor::create({
		.outputName = modelManifest.outputs.front().name,
		.threshold = profile.pipeline.threshold,
	});
	if (!postprocessor) {
		return failure<AssembledSession>(postprocessor.status().code(),
			std::string(postprocessor.status().message()));
	}

	pipeline::PipelineBuilder<SessionResult> builder;
	auto builtPipeline = builder
		.setPreprocessor(std::move(preprocessor).value())
		.setBackend(std::move(backend).value())
		.setPostprocessor(std::move(postprocessor).value())
		.build();
	if (!builtPipeline) {
		return failure<AssembledSession>(builtPipeline.status().code(),
			std::string(builtPipeline.status().message()));
	}

	std::unique_ptr<pipeline::IStagedVisionPipeline<SessionResult>>
		stagedPipeline = std::make_unique<benchmark::TimedPipeline<SessionResult>>(
			std::move(builtPipeline).value(),
			std::move(timingObserver),
			std::move(batchObserver));
	assembled.timedPipeline = static_cast<
		benchmark::TimedPipeline<SessionResult>*>(stagedPipeline.get());

	const auto maxBatchSize = profile.pipeline.maxBatchSize > 0
		? profile.pipeline.maxBatchSize
		: profile.sources.size();
	auto executorResult = runtime::RuntimeFactory::createBatchExecutor<
		SessionResult>(
		std::move(stagedPipeline), deployment.value(),
		executor::BatchInferenceOptions{
			.maxBatchSize = maxBatchSize,
			.flushTimeout = profile.pipeline.flushTimeout,
		});
	if (!executorResult) {
		return failure<AssembledSession>(executorResult.status().code(),
			std::string(executorResult.status().message()));
	}

	runtime::MultiCameraExecutionOptions<SessionResult> options;
	options.sourceFailureCallback =
		[&registry](const core::Status& status) {
			static_cast<void>(status);
			static_cast<void>(registry.publishState("session.state"));
		};
	options.droppedFrameCallback =
		[&registry](const core::Status& status) {
			static_cast<void>(status);
			static_cast<void>(registry.publishState("session.state"));
		};
	options.frameObserver = [&registry, streamBindings](
		std::uint32_t sourceId, const vision::Frame& frame) {
		if (sourceId >= streamBindings->size()) {
			return;
		}
		auto view = vision::Frame::create(
			frame.buffer(), frame.width(), frame.height(), frame.pixelFormat(),
			frame.rowStride(), frame.metadata(), frame.byteOffset());
		if (view) {
			registry.publishFrame(
				(*streamBindings)[sourceId].second, sourceId,
				std::move(view).value());
		}
	};
	assembled.session = std::make_unique<
		runtime::MultiCameraSession<SessionResult>>(
		std::move(sources), std::move(executorResult).value(),
		std::move(options));
	assembled.streamBindings = std::move(streamBindings);

	return core::Result<AssembledSession>::success(std::move(assembled));
}

} // namespace visionService::session
