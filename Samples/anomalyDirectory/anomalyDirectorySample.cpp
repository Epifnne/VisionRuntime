#include <visionruntime>

int main() {
	using namespace visionRuntime;
	auto session = runtime::AnomalyRuntimeFactory::create({
		.source = {
			.directory = "image",
		},
		.model = {
			.path = "model/model.onnx",
		},
		.threshold = 2.0F,
		.timed = true,
		.callback = [](executor::TaskId,
			const core::Result<vision::AnomalyResult>&) {},
	}).value();
	session->start().value();
	static_cast<void>(session->wait());
}
