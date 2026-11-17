#include "exports/productEntry.hpp"
#include "../common/profileCli.hpp"

int main(int argc, char* argv[]) {
	return visionSample::runProfileCli(argc, argv, visionProduct::createApplication, "routing.result");
}
