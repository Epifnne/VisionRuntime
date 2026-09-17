#pragma once

#include "backends/iInferenceBackend.hpp"
#include "backends/pluginInferenceBackend.hpp"
#include "benchmark/anomalyCsvTimedPipeline.hpp"
#include "camera/frameSourceFactory.hpp"
#include "config/deploymentConfig.hpp"
#include "config/modelManifest.hpp"
#include "config/modelPackageLoader.hpp"
#include "logs/logger.hpp"
#include "pipeline/pipelineBuilder.hpp"
#include "postProcess/anomalyThresholdPostprocessor.hpp"
#include "preProcess/frameNodes/cvCenterCropNode.hpp"
#include "preProcess/frameNodes/cvResizeNode.hpp"
#include "preProcess/frameNodes/toTensorNode.hpp"
#include "preProcess/iPreProcessor.hpp"
#include "preProcess/preprocessChain.hpp"
#include "preProcess/tensorNodes/normalizeNode.hpp"
#include "runtime/runtimeFactory.hpp"
#include "vision/anomalyResult.hpp"

#include <cmath>
#include <filesystem>
#include <memory>
#include <utility>

namespace visionRuntime::runtime::presets {

struct AnomalyModelOptions {
	std::filesystem::path packagePath;
};

struct AnomalyRuntimeOptions {
	camera::FrameSourceConfig source = camera::FileFrameSourceConfig{};
	AnomalyModelOptions model{};
	float threshold = 0.5F;
	bool timed = false;
	benchmark::TimingOutputPath timingOutput =
		benchmark::TimingOutputPath::standardOutput();
	config::DeploymentConfig deployment{
		.executor = {
			.performancePolicy = config::PerformancePolicy::PipelineParallel,
			.queueFullPolicy = config::QueueFullPolicy::Block,
			.queueCapacity = 16,
			.stageQueueCapacity = 2,
		},
	};
	executor::CompletionCallback<vision::AnomalyResult> callback{};
};

class AnomalyPreset {
public:
	using Options = AnomalyRuntimeOptions;
	using Session = RuntimeSession<vision::AnomalyResult>;

	[[nodiscard]] static core::Result<std::unique_ptr<Session>> create(Options options) {
		if (!std::isfinite(options.threshold)) {
			return failure("anomaly threshold must be finite");
		}
		if (options.deployment.backend.id.empty() ||
			options.deployment.backend.device.empty() ||
			options.deployment.backend.pluginDirectory.empty()) {
			return failure("backend deployment requires pluginDirectory, id, and device");
		}
		auto package = config::ModelPackageLoader::load(options.model.packagePath);
		if (!package) {
			return core::Result<std::unique_ptr<Session>>::failure(package.status());
		}
		auto manifestStatus = validateManifest(package->manifest());
		if (!manifestStatus) {
			return core::Result<std::unique_ptr<Session>>::failure(
				manifestStatus.status());
		}
		auto selected = package->selectArtifact(options.deployment.backend);
		if (!selected) {
			return core::Result<std::unique_ptr<Session>>::failure(selected.status());
		}

		auto sourceResult = camera::FrameSourceFactory::create(options.source);
		if (!sourceResult) {
			return core::Result<std::unique_ptr<Session>>::failure(sourceResult.status());
		}
		auto source = std::move(sourceResult).value();
		const auto frameCount = source->info().expectedFrameCount;
		const auto& manifest = package->manifest();

		preprocess::ToTensorOptions toTensorOptions{
			.tensorName = manifest.inputs.front().name,
			.bufferCount = options.deployment.executor.stageQueueCapacity + 2,
			.channels = 1,
		};
		preprocess::NormalizeOptions normalizeOptions{
			.mean = {0.449F},
			.standardDeviation = {0.226F},
		};
		auto builtPreprocessor = preprocess::PreprocessBuilder::start<vision::Frame>()
			.then(preprocess::CvResize::shortSide(256))
			.then(preprocess::CvCenterCrop({224, 224}))
			.then(preprocess::ToTensor(std::move(toTensorOptions)))
			.then(preprocess::Normalize(std::move(normalizeOptions)))
			.build();
		if (!builtPreprocessor) {
			return core::Result<std::unique_ptr<Session>>::failure(builtPreprocessor.status());
		}
		auto preprocessor = std::move(builtPreprocessor).value();

		auto builtBackend = backends::PluginInferenceBackend::create({
			.pluginPath = backends::backendPluginPath(
				options.deployment.backend.pluginDirectory, options.deployment.backend.id),
			.backendId = options.deployment.backend.id,
			.artifactPath = selected->artifactPath,
			.device = selected->device,
			.optionsJson = selected->optionsJson,
			.artifactKind = selected->artifactKind,
			.inputName = manifest.inputs.front().name,
			.outputName = manifest.outputs.front().name,
		});
		if (!builtBackend) {
			return core::Result<std::unique_ptr<Session>>::failure(builtBackend.status());
		}
		auto backend = std::move(builtBackend).value();

		auto builtPostprocessor = postprocess::AnomalyThresholdPostprocessor::create({
			.outputName = manifest.outputs.front().name,
			.threshold = options.threshold,
		});
		if (!builtPostprocessor) {
			return core::Result<std::unique_ptr<Session>>::failure(builtPostprocessor.status());
		}
		auto postprocessor = std::move(builtPostprocessor).value();

		pipeline::PipelineBuilder<vision::AnomalyResult> builder;
		auto pipelineResult = builder
			.setPreprocessor(std::move(preprocessor))
			.setBackend(std::move(backend))
			.setPostprocessor(std::move(postprocessor))
			.build();
		if (!pipelineResult) {
			return core::Result<std::unique_ptr<Session>>::failure(
				pipelineResult.status());
		}
		auto timedPipeline = benchmark::makeAnomalyCsvTimedPipeline(
			std::move(pipelineResult).value(), {
				.activate = options.timed,
				.outputPath = std::move(options.timingOutput),
			});
		if (!timedPipeline) {
			return core::Result<std::unique_ptr<Session>>::failure(
				timedPipeline.status());
		}

		executor::FrameExecutionOptions<vision::AnomalyResult> executionOptions;
		executionOptions.frameCount = frameCount;
		executionOptions.completionCallback = std::move(options.callback);
		return RuntimeFactory::createRuntime(
			std::move(source), std::move(timedPipeline).value(), options.deployment,
			std::move(executionOptions));
	}

private:
	[[nodiscard]] static core::Result<void> validateManifest(
		const config::ModelManifest& manifest) {
		if (manifest.inputs.size() != 1 || manifest.outputs.size() != 1) {
			return invalidManifest(
				"anomaly preset requires exactly one input and one output");
		}
		const auto& input = manifest.inputs.front();
		if (input.name.empty() || input.elementType != config::TensorElementType::Float32 ||
			input.layout != config::TensorLayout::Nchw ||
			input.shape.size() != 4 || input.shape[0] == 0 ||
			input.shape[1] != 1 || input.shape[2] != 224 || input.shape[3] != 224) {
			return invalidManifest(
				"anomaly preset input must be Float32 NCHW [N,1,224,224]");
		}
		const auto& output = manifest.outputs.front();
		if (output.name.empty() ||
			output.elementType != config::TensorElementType::Float32 ||
			output.layout != config::TensorLayout::Scalar ||
			output.shape != std::vector<std::size_t>{1}) {
			return invalidManifest(
				"anomaly preset output must be a Float32 scalar [1]");
		}
		return core::Result<void>::success();
	}

	[[nodiscard]] static core::Result<void> invalidManifest(const char* message) {
		auto status = core::Status::error(core::StatusCode::InvalidArgument, message);
		logs::report(status);
		return core::Result<void>::failure(std::move(status));
	}

	[[nodiscard]] static core::Result<std::unique_ptr<Session>> failure(
		const char* message) {
		auto status = core::Status::error(core::StatusCode::InvalidArgument, message);
		logs::report(status);
		return core::Result<std::unique_ptr<Session>>::failure(std::move(status));
	}
};

} // namespace visionRuntime::runtime::presets