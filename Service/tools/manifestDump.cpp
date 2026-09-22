/**
 * @file manifestDump.cpp
 * @brief Prints the endpoint registry manifest JSON for a product profile.
 *
 * The manifest is the contract between visionService and visionDesigner:
 * the designer loads this file (or derives the same list from a profile) to
 * populate its bindable endpoint tree.
 *
 * Usage: visionManifestDump <profile.json> [output.json]
 */

#include "service/visionService.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(int argc, char* argv[]) {
	if (argc < 2 || argc > 3) {
		std::fprintf(stderr,
			"Usage: visionManifestDump <profile.json> [output.json]\n");
		return 1;
	}

	visionService::VisionService service;
	auto loaded = service.loadProfile(std::filesystem::path(argv[1]));
	if (!loaded) {
		std::fprintf(stderr, "failed to load profile: %s\n",
			std::string(loaded.status().message()).c_str());
		return 1;
	}

	const std::string manifest = service.exportManifest();
	if (argc == 3) {
		std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
		if (!output) {
			std::fprintf(stderr, "cannot open output file: %s\n", argv[2]);
			return 1;
		}
		output << manifest << '\n';
		return 0;
	}
	std::cout << manifest << '\n';
	return 0;
}
