#include "productShell.hpp"
#include "profileCli.hpp"

int main(int argc, char* argv[]) {
	return visionSample::runProfileCli(argc, argv, visionSample::createProductRuntime);
}
