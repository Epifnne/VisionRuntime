#pragma once

#include "backends/iInferenceBackend.hpp"
#include "backends/openVinoBackend.hpp"
#include "benchmark/anomalyCsvTimedPipeline.hpp"
#include "camera/fileSource.hpp"
#include "config/buildProfile.hpp"
#include "config/deploymentConfig.hpp"
#include "config/modelManifest.hpp"
#include "pipeline/pipelineBuilder.hpp"
#include "postProcess/anomalyPostprocessor.hpp"
#include "postProcess/anomalyThresholdPostprocessor.hpp"
#include "preProcess/frameNodes/centerCropNode.hpp"
#include "preProcess/frameNodes/resizeNode.hpp"
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

[[nodiscard]] inline config::ModelManifest anomalyModelManifest() {
	return {
		.inputs = {{
			.name = "image",
			.elementType = config::TensorElementType::Float32,
			.layout = config::TensorLayout::Nchw,
			.shape = {1, 1, 224, 224},
		}},
		.outputs = {{
			.name = "score",
			.elementType = config::TensorElementType::Float32,
			.layout = config::TensorLayout::Scalar,
			.shape = {1},
		}},
	};
}

struct AnomalyModelOptions {
	std::filesystem::path path;
	config::ModelManifest manifest = anomalyModelManifest();
	std::string device = "CPU";
};

struct AnomalyRuntimeOptions {
	camera::FileSourceOptions source;
	AnomalyModelOptions model;
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
	executor::CompletionCallback<vision::AnomalyResult> callback;
	std::unique_ptr<preprocess::IPreprocessor> preprocessor;
	std::unique_ptr<backends::IInferenceBackend> backend;
	std::unique_ptr<postprocess::IPostprocessor<vision::AnomalyResult>> postprocessor;
};

class AnomalyPreset {
public:
	using Options = AnomalyRuntimeOptions;
	using Session = RuntimeSession<vision::AnomalyResult>;

	[[nodiscard]] static core::Result<std::unique_ptr<Session>> create(Options options) {
		auto manifestStatus = validateManifest(options.model.manifest);
		if (!manifestStatus) {
			return core::Result<std::unique_ptr<Session>>::failure(
				manifestStatus.status());
		}
		if (!std::isfinite(options.threshold)) {
			return failure("anomaly threshold must be finite");
		}

		auto sourceResult = camera::FileSource::create(std::move(options.source));
		if (!sourceResult) {
			return core::Result<std::unique_ptr<Session>>::failure(sourceResult.status());
		}
		auto source = std::move(sourceResult).value();
		const auto frameCount = source->imageCount();

		auto preprocessor = std::move(options.preprocessor);
		if (!preprocessor) {
			auto built = preprocess::PreprocessBuilder::start<vision::Frame>()
				.then(preprocess::Resize::shortSide(256))
				.then(preprocess::CenterCrop({224, 224}))
				.then(preprocess::ToTensor({
					.tensorName = options.model.manifest.inputs.front().name,
					.bufferCount = options.deployment.executor.stageQueueCapacity + 2,
					.channels = 1,
				}))
				.then(preprocess::Normalize({
					.mean = {0.449F},
					.standardDeviation = {0.226F},
				}))
				.build();
			if (!built) {
				return core::Result<std::unique_ptr<Session>>::failure(built.status());
			}
			preprocessor = std::move(built).value();
		}

		auto backend = std::move(options.backend);
		if (!backend) {
			auto built = backends::OpenVinoBackend::create({
				.modelPath = std::move(options.model.path),
				.device = std::move(options.model.device),
				.inputName = options.model.manifest.inputs.front().name,
				.outputName = options.model.manifest.outputs.front().name,
			});
			if (!built) {
				return core::Result<std::unique_ptr<Session>>::failure(built.status());
			}
			backend = std::move(built).value();
		}

		auto postprocessor = std::move(options.postprocessor);
		if (!postprocessor) {
			auto score = postprocess::AnomalyPostprocessor::create({
				.outputName = options.model.manifest.outputs.front().name,
			});
			if (!score) {
				return core::Result<std::unique_ptr<Session>>::failure(score.status());
			}
			auto threshold = postprocess::AnomalyThresholdPostprocessor::create(
				std::move(score).value(), {.threshold = options.threshold});
			if (!threshold) {
				return core::Result<std::unique_ptr<Session>>::failure(threshold.status());
			}
			postprocessor = std::move(threshold).value();
		}

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
			input.shape != std::vector<std::size_t>{1, 1, 224, 224}) {
			return invalidManifest(
				"anomaly preset input must be Float32 NCHW [1,1,224,224]");
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
		return core::Result<void>::failure(
			core::Status::error(core::StatusCode::InvalidArgument, message));
	}

	[[nodiscard]] static core::Result<std::unique_ptr<Session>> failure(
		const char* message) {
		return core::Result<std::unique_ptr<Session>>::failure(
			core::Status::error(core::StatusCode::InvalidArgument, message));
	}
};

} // namespace visionRuntime::runtime::presets

namespace visionRuntime::runtime {

using AnomalyRuntimeOptions = presets::AnomalyRuntimeOptions;

class AnomalyRuntimeFactory {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<
		RuntimeSession<vision::AnomalyResult>>> create(AnomalyRuntimeOptions options) {
		return RuntimeFactory::createFromPreset<presets::AnomalyPreset>(
			std::move(options));
	}
};

} // namespace visionRuntime::runtime