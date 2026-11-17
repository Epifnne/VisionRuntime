#include "exports/productEntry.hpp"
#include "service/visionService.hpp"
#include "profile/profileLoader.hpp"

#include <fstream>
#include <iostream>
#include <thread>
#include <charconv>
#ifdef VISION_MEASUREMENT_CUDA
#include "cudaThresholdAlgorithm.hpp"
#endif

int main(int argc, char* argv[]) {
	if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
#ifdef VISION_MEASUREMENT_CUDA
		int device = 0;
		if (argc > 3) { std::cerr << "usage: --self-test [device-index]\n"; return 1; }
		if (argc == 3) {
			const std::string_view text(argv[2]);
			const auto parsed = std::from_chars(text.data(), text.data() + text.size(), device);
			if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || device < 0) {
				std::cerr << "device-index must be a nonnegative integer\n";
				return 1;
			}
		}
		return visionSample::openCvCuda::runCudaSelfTest(device);
#else
		std::cerr << "CUDA self-test requires VISION_WITH_OPENCV_CUDA=ON\n";
		return 1;
#endif
	}
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
	return summary.completed > 0 && summary.failed == 0 && summary.sourceFailures == 0 &&
		summary.dropped == 0 && summary.rejected == 0 ? 0 : 2;
}
