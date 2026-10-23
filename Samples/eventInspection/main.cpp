#include "exports/productEntry.hpp"
#include "service/visionService.hpp"
#include "profile/profileLoader.hpp"

#include <fstream>
#include <iostream>

int main(int argc, char* argv[]) {
	std::filesystem::path profilePath = VISION_EVENT_SAMPLE_PROFILE;
	bool missing = false;
	std::filesystem::path manifestPath;
	for (int index = 1; index < argc; ++index) {
		const std::string_view argument(argv[index]);
		if (argument == "--missing-view") { missing = true; }
		else if (argument == "--export-manifest" && index + 1 < argc) { manifestPath = argv[++index]; }
		else if (!argument.starts_with("--")) { profilePath = argv[index]; }
		else {
			std::cerr << "usage: eventInspectionSample [profile.json] [--missing-view] "
				"[--export-manifest output.json]\n";
			return 1;
		}
	}
	if (!manifestPath.empty()) {
		auto profile = visionService::profile::ProfileLoader::load(profilePath);
		if (!profile) { std::cerr << profile.status().toString() << '\n'; return 1; }
		auto manifest = visionProduct::describeApplication(profile.value());
		if (!manifest) { std::cerr << manifest.status().toString() << '\n'; return 1; }
		std::ofstream output(manifestPath);
		output << manifest.value() << '\n';
		output.close();
		if (!output) { std::cerr << "cannot write manifest\n"; return 1; }
		return 0;
	}
	visionService::VisionService service([missing](auto profile) {
		if (missing) {
			auto document = nlohmann::json::parse(profile.documentJson);
			document["simulation"]["missingView"] = true;
			profile.documentJson = document.dump();
		}
		return visionProduct::createApplication(profile);
	});
	auto loaded = service.loadProfile(profilePath);
	if (!loaded) { std::cerr << loaded.status().toString() << '\n'; return 1; }
	auto profile = visionService::profile::ProfileLoader::load(profilePath);
	if (!profile) { std::cerr << profile.status().toString() << '\n'; return 1; }
	try {
		const auto document = nlohmann::json::parse(profile->documentJson);
		missing = missing || document.value("simulation", nlohmann::json::object()).value("missingView", false);
	} catch (const nlohmann::json::exception& exception) {
		std::cerr << "invalid simulation configuration: " << exception.what() << '\n';
		return 1;
	}
	nlohmann::json last;
	std::size_t results = 0;
	auto subscribed = service.endpoints().subscribeState("workpiece.result", [&](const auto& snapshot) {
		last = nlohmann::json::parse(snapshot.payload);
		++results;
		std::cout << snapshot.payload << '\n';
	});
	if (!subscribed) { std::cerr << subscribed.status().toString() << '\n'; return 1; }
	auto started = service.endpoints().invokeCommand("session.start");
	if (!started) { std::cerr << started.status().toString() << '\n'; return 1; }
	service.sessionController().waitForIdle();
	service.shutdown();
	const auto counts = service.sessionController().counters();
	if (results != 4 || last.value("okCount", 0) != (missing ? 1 : 2) ||
		last.value("ngCount", 0) != 2 || last.value("faultCount", 0) != (missing ? 1 : 0) ||
		counts.received != (missing ? 7 : 8) || counts.completed != (missing ? 7 : 8) ||
		counts.failed || counts.sourceFailures || !service.sessionController().lastError().empty()) {
		std::cerr << "unexpected event product results or frame counters\n";
		return 2;
	}
	return 0;
}
