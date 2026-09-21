#include "backends/backendPluginApi.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace {

struct FakeBackend {
	bool writesUnterminatedError = false;
};

bool equals(VisionRuntimeStringView value, const char* expected) {
	const auto length = std::strlen(expected);
	return value.size == length && std::memcmp(value.data, expected, length) == 0;
}

void writeError(VisionRuntimeErrorBuffer error, const char* message) {
	if (error.capacity == 0) {
		return;
	}
	const auto length = std::min(std::strlen(message), error.capacity - 1);
	std::memcpy(error.data, message, length);
	error.data[length] = '\0';
}

VisionRuntimeBackendStatus create(
	const VisionRuntimeBackendCreateOptions* options,
	void** backend,
	VisionRuntimeErrorBuffer error) {
	if (equals(options->optionsJson, "nullHandle")) {
		return VISION_RUNTIME_BACKEND_STATUS_OK;
	}
	if (equals(options->optionsJson, "verifyUtf8Path")) {
		constexpr char expected[] = "\xE6\xA8\xA1\xE5\x9E\x8B.engine";
		if (!equals(options->artifactPath, expected)) {
			writeError(error, "artifact path is not UTF-8");
			return VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT;
		}
	}
	*backend = new (std::nothrow) FakeBackend{
		.writesUnterminatedError = equals(options->optionsJson, "unterminatedError"),
	};
	return *backend == nullptr
		? VISION_RUNTIME_BACKEND_STATUS_INTERNAL
		: VISION_RUNTIME_BACKEND_STATUS_OK;
}

void destroy(void* backend) {
	delete static_cast<FakeBackend*>(backend);
}

VisionRuntimeBackendStatus infer(
	void* rawBackend,
	const VisionRuntimeTensorView* inputs,
	size_t inputCount,
	VisionRuntimeOutputAllocator outputs,
	VisionRuntimeErrorBuffer error) {
	const auto& backend = *static_cast<FakeBackend*>(rawBackend);
	if (inputCount != 1) {
		if (backend.writesUnterminatedError) {
			std::memset(error.data, 'x', error.capacity);
			return VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT;
		}
		writeError(error, "fake backend requires one input");
		return VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT;
	}
	const auto& input = inputs[0];
	void* outputData = nullptr;
	const VisionRuntimeStringView outputName{"output", 6};
	const auto allocationStatus = outputs.allocate(
		outputs.context, outputName, input.dataType, input.dimensions,
		input.rank, input.byteSize, &outputData);
	if (allocationStatus != VISION_RUNTIME_BACKEND_STATUS_OK) {
		return allocationStatus;
	}
	const auto elementCount = input.byteSize / sizeof(float);
	const auto* inputData = static_cast<const float*>(input.data);
	auto* result = static_cast<float*>(outputData);
	for (size_t index = 0; index < elementCount; ++index) {
		result[index] = inputData[index] * 2.0F;
	}
	return VISION_RUNTIME_BACKEND_STATUS_OK;
}

constexpr VisionRuntimeStringView backendId{"fake", 4};
const VisionRuntimeBackendApi api{
	.structSize = sizeof(VisionRuntimeBackendApi),
	.abiMajor = VISION_RUNTIME_BACKEND_ABI_MAJOR,
	.abiMinor = VISION_RUNTIME_BACKEND_ABI_MINOR,
	.backendId = backendId,
	.create = create,
	.destroy = destroy,
	.infer = infer,
};

} // namespace

extern "C" VISION_RUNTIME_BACKEND_EXPORT const VisionRuntimeBackendApi*
visionRuntimeBackendQuery(void) {
	return &api;
}
