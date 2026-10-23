#include "exports/productEntry.hpp"
#include "profile/profileLoader.hpp"

#include <fstream>
#include <iostream>

int main(int argc, char* argv[]) {
	if (argc != 3) {
		std::cerr << "usage: visionProductManifestGenerator <profile.json> <output.json>\n";
		return 1;
	}
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
	std::ofstream output(argv[2], std::ios::binary);
	output << manifest.value() << '\n';
	output.close();
	if (!output) {
		std::cerr << "cannot write product manifest: " << argv[2] << '\n';
		return 1;
	}
	return 0;
}
