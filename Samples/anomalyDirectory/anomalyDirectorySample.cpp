#include <visionruntime>

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
	if (argc != 4) {
		std::cerr << "usage: anomalyDirectorySample <benchmark.csv> <model-package> "
			"<deployment.json>\n";
		return 1;
	}

	using namespace visionRuntime;
	auto deployment = config::ConfigLoader::loadDeployment(argv[3]);
	if (!deployment) {
		return 1;
	}
	auto sessionResult = runtime::RuntimeFactory::createFromPreset<
		runtime::presets::AnomalyPreset>({
		.source = camera::FileFrameSourceConfig{
			.source = {
				.directory = "image",
			},
		},
		.model = {
			.packagePath = argv[2],
		},
		.threshold = 2.0F,
		.timed = true,
		.timingOutput = benchmark::TimingOutputPath::file(
			std::filesystem::path{argv[1]}),
		.deployment = std::move(deployment).value(),
		.callback = [](executor::TaskId,
			const core::Result<vision::AnomalyResult>&) {},
	});
	if (!sessionResult) {
		return 1;
	}
	auto session = std::move(sessionResult).value();
	auto startResult = session->start();
	if (!startResult) {
		return 1;
	}
	static_cast<void>(session->wait());
}
