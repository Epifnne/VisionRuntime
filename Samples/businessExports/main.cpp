#include "exports/productEntry.hpp"
#include "service/visionService.hpp"
#include "profile/profileLoader.hpp"

#include <fstream>
#include <iostream>
#include <thread>

int main(int argc, char* argv[]) {
	if (argc != 2 && argc != 4) {
		std::cerr << "usage: businessExportsSample <profile.json> [--export-manifest <output.json>]\n";
		return 1;
	}
	if (argc == 4 && std::string_view(argv[2]) != "--export-manifest") {
		std::cerr << "unknown option: " << argv[2] << '\n';
		return 1;
	}
	if (argc == 4) {
		auto profile = visionService::profile::ProfileLoader::load(argv[1]);
		if (!profile) {
			std::cerr << profile.status().toString() << '\n';
			return 1;
		}
		auto manifest = visionProduct::describeApplication(profile.value());
		if (!manifest) {
			std::cerr << manifest.status().toString() << '\n';
			return 1;
		}
		std::ofstream output(argv[3], std::ios::binary);
		output << manifest.value() << '\n';
		output.close();
		if (!output) {
			std::cerr << "cannot write manifest: " << argv[3] << '\n';
			return 1;
		}
		return 0;
	}
	visionService::VisionService service(visionProduct::createApplication);
	auto loaded = service.loadProfile(argv[1]);
	if (!loaded) {
		std::cerr << loaded.status().toString() << '\n';
		return 1;
	}
	auto subscription = service.endpoints().subscribeState("measure.result", [](const auto& snapshot) {
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
	return summary.failed == 0 && summary.sourceFailures == 0 &&
		summary.dropped == 0 && summary.rejected == 0 ? 0 : 2;
}
