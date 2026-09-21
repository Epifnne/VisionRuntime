#include "backends/backendPluginApi.h"

namespace {

constexpr VisionRuntimeStringView backendId{"incompatible", 12};
const VisionRuntimeBackendApi api{
	.structSize = sizeof(VisionRuntimeBackendApi),
	.abiMajor = VISION_RUNTIME_BACKEND_ABI_MAJOR + 1,
	.abiMinor = 0,
	.backendId = backendId,
};

} // namespace

extern "C" VISION_RUNTIME_BACKEND_EXPORT const VisionRuntimeBackendApi*
visionRuntimeBackendQuery(void) {
	return &api;
}
