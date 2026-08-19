#include <visionruntime>

#include <benchmark/anomalyCsvTimedPipeline.hpp>

#include <cstddef>

namespace {

constexpr std::size_t executorQueueCapacity = 16;

} // namespace

int main() {
	using namespace visionRuntime;

	auto preprocessor = preprocess::PreprocessBuilder::start<vision::Frame>()
		.then(preprocess::Resize::shortSide(256))
		.then(preprocess::CenterCrop({224, 224}))
		.then(preprocess::ToTensor({
			.tensorName = "image",
			.channels = 1,
		}))
		.then(preprocess::Normalize({
			.mean = {0.449F},
			.standardDeviation = {0.226F},
		}))
		.build().value();

	auto backend = backends::OpenVinoBackend::create({
		.modelPath = "model/model.onnx",
		.device = VISION_RUNTIME_DEVICE_NAME,
		.inputName = "image",
		.outputName = "score",
	}).value();

	auto scorePostprocessor = postprocess::AnomalyPostprocessor::create({
		.outputName = "score",
	}).value();
	
	auto thresholdPostprocessor = postprocess::AnomalyThresholdPostprocessor::create(
		std::move(scorePostprocessor), {
			.threshold = 2.0F,
		}).value();

	pipeline::PipelineBuilder<vision::AnomalyResult> builder;
	auto pipeline = builder
		.setPreprocessor(std::move(preprocessor))
		.setBackend(std::move(backend))
		.setPostprocessor(std::move(thresholdPostprocessor))
		.build().value();

	auto timedPipeline = benchmark::makeAnomalyCsvTimedPipeline(
		std::move(pipeline), {
			.activate = true,
			.outputPath = benchmark::TimingOutputPath::standardOutput(),
		}).value();

	auto source = camera::FileSource::create({
		.directory = "image",
	}).value();
	const auto frameCount = source->imageCount();

	executor::FrameExecutor<vision::AnomalyResult> frameExecutor(
		std::move(source),
		std::move(timedPipeline),
		{
			.executor = {
				.queueCapacity = executorQueueCapacity,
				.queueFullPolicy = executor::QueueFullPolicy::Block,
			},
			.frameCount = frameCount,
		});
	frameExecutor.start().value();
	static_cast<void>(frameExecutor.wait());
}
