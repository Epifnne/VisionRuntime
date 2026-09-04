#include <visionruntime>

#include "runtime/presets/anomalyPreset.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
	if (argc != 2) {
		std::cerr << "usage: anomalyDirectorySample <benchmark.csv>\n";
		return 1;
	}

	using namespace visionRuntime;
	auto session = runtime::RuntimeFactory::createFromPreset<
		runtime::presets::AnomalyPreset>({
		.source = camera::FileFrameSourceConfig{
			.source = {
				.directory = "image",
			},
		},
		.model = {
			.path = "model/model-fp32.engine",
			.manifest = {
				.inputs = {{
					.name = "images",
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
			},
			.inferenceThreads = 8,
		},
		.threshold = 2.0F,
		.timed = true,
		.timingOutput = benchmark::TimingOutputPath::file(
			std::filesystem::path{argv[1]}),
		.deployment = {
			.executor = {
				.performancePolicy = config::PerformancePolicy::Serial,
				.queueFullPolicy = config::QueueFullPolicy::Block,
				.queueCapacity = 16,
				.stageQueueCapacity = 1,
			},
		},
		.callback = [](executor::TaskId,
			const core::Result<vision::AnomalyResult>&) {},
	}).value();
	session->start().value();
	static_cast<void>(session->wait());
}
