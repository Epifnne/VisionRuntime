#include <visionruntime>

#include <atomic>
#include <filesystem>
#include <iostream>
#include <utility>

int main(int argc, char* argv[]) {
	if (argc != 3) {
		std::cerr << "usage: anomalyHikMvsSample <model-package> <deployment.json>\n";
		return 1;
	}

	using namespace visionRuntime;
	auto deployment = config::ConfigLoader::loadDeployment(argv[2]);
	if (!deployment) {
		return 1;
	}
	std::atomic_bool firstResultPrinted = false;
	auto sessionResult = runtime::RuntimeFactory::createFromPreset<
		runtime::presets::AnomalyPreset>({
		.source = camera::ContinuousCameraSourceConfig{
			.device = {
				.ipAddress = "169.254.239.231",
				.pixelFormat = vision::PixelFormat::Gray8,
			},
		},
		.model = {
			.packagePath = argv[1],
		},
		.threshold = 2.0F,
		.deployment = std::move(deployment).value(),
		.callback = [&firstResultPrinted](executor::TaskId,
			const core::Result<vision::AnomalyResult>& result) {
			if (!result || firstResultPrinted.exchange(true)) {
				return;
			}
			std::cerr << "score=" << result->score
				<< ", threshold=" << result->threshold
				<< ", decision="
				<< vision::anomalyDecisionName(result->decision) << '\n';
		},
	});
	if (!sessionResult) {
		return 1;
	}
	auto session = std::move(sessionResult).value();
	auto startResult = session->start();
	if (!startResult) {
		return 1;
	}
	const auto summary = session->wait();
	std::cerr << "received=" << summary.received
		<< ", submitted=" << summary.submitted
		<< ", completed=" << summary.completed
		<< ", failed=" << summary.failed
		<< ", dropped=" << summary.dropped
		<< ", sourceFailures=" << summary.sourceFailures << '\n';
}
