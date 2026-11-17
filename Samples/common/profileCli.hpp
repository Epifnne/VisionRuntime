#pragma once

#include "service/visionService.hpp"

#include <iostream>

namespace visionSample {

template<typename Factory>
int runProfileCli(int argc, char* argv[], Factory factory,
	const char* resultEndpoint = "inspection.result") {
	if (argc != 2) {
		std::cerr << "usage: " << argv[0] << " <profile.json>\n";
		return 1;
	}
	visionService::VisionService service(std::move(factory));
	auto loaded = service.loadProfile(argv[1]);
	if (!loaded) {
		std::cerr << loaded.status().toString() << '\n';
		return 1;
	}
	auto subscription = service.endpoints().subscribeState(resultEndpoint, [](const auto& snapshot) {
		std::cout << snapshot.payload << '\n';
	});
	if (!subscription) {
		std::cerr << subscription.status().toString() << '\n';
		return 1;
	}
	auto started = service.endpoints().invokeCommand("session.start");
	if (!started) {
		std::cerr << started.status().toString() << '\n';
		return 1;
	}
	service.sessionController().waitForIdle();
	service.shutdown();
	const auto summary = service.sessionController().counters();
	std::cout << "completed=" << summary.completed << " failed=" << summary.failed
		<< " dropped=" << summary.dropped << " sourceFailures=" << summary.sourceFailures << '\n';
	return summary.completed == 0 || summary.failed || summary.sourceFailures ||
		summary.dropped || summary.rejected || !service.sessionController().lastError().empty() ? 2 : 0;
}

} // namespace visionSample
