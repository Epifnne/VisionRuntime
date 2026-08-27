#include <visionruntime>

#include "runtime/presets/anomalyPreset.hpp"

int main() {
	using namespace visionRuntime;
	auto session = runtime::AnomalyRuntimeFactory::create({
		.source = camera::ContinuousCameraSourceConfig{
			.device = {
				.ipAddress = "192.168.1.64",
				.pixelFormat = vision::PixelFormat::Gray8,
			},
		},
		.model = {
			.path = "../anomalyDirectory/model/model-int8.onnx",
			.inferenceThreads = 8,
		},
		.threshold = 2.0F,
		.callback = [](executor::TaskId,
			const core::Result<vision::AnomalyResult>&) {},
	}).value();
	session->start().value();
	static_cast<void>(session->wait());
}
