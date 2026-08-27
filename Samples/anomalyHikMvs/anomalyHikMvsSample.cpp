#include <visionruntime>

#include "runtime/presets/anomalyPreset.hpp"

#include <atomic>
#include <iostream>
#include <utility>

int main() {
	using namespace visionRuntime;
	std::atomic_bool firstResultPrinted = false;
	auto sessionResult = runtime::AnomalyRuntimeFactory::create({
		.source = camera::ContinuousCameraSourceConfig{
			.device = {
				.ipAddress = "169.254.239.231",
				.pixelFormat = vision::PixelFormat::Gray8,
			},
		},
		.model = {
			.path = "../anomalyDirectory/model/model-int8.onnx",
			.inferenceThreads = 8,
		},
		.threshold = 2.0F,
		.callback = [&firstResultPrinted](executor::TaskId,
			const core::Result<vision::AnomalyResult>& result) {
			if (firstResultPrinted.exchange(true)) {
				return;
			}
			if (!result) {
				std::cerr << result.status().toString() << '\n';
				return;
			}
			std::cerr << "score=" << result->score
				<< ", threshold=" << result->threshold
				<< ", decision="
				<< vision::anomalyDecisionName(result->decision) << '\n';
		},
	});
	if (!sessionResult) {
		std::cerr << sessionResult.status().toString() << '\n';
		return 1;
	}
	auto session = std::move(sessionResult).value();
	auto startResult = session->start();
	if (!startResult) {
		std::cerr << startResult.status().toString() << '\n';
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
