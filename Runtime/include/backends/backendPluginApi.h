#pragma once

#include <stddef.h>
#include <stdint.h>

#define VISION_RUNTIME_BACKEND_ABI_MAJOR 1u
#define VISION_RUNTIME_BACKEND_ABI_MINOR 0u
#define VISION_RUNTIME_BACKEND_ENTRY_POINT "visionRuntimeBackendQuery"

#if defined(_WIN32) && defined(VISION_RUNTIME_BACKEND_PLUGIN_EXPORTS)
#define VISION_RUNTIME_BACKEND_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) && defined(VISION_RUNTIME_BACKEND_PLUGIN_EXPORTS)
#define VISION_RUNTIME_BACKEND_EXPORT __attribute__((visibility("default")))
#else
#define VISION_RUNTIME_BACKEND_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t VisionRuntimeBackendStatus;
#define VISION_RUNTIME_BACKEND_STATUS_OK 0u
#define VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT 1u
#define VISION_RUNTIME_BACKEND_STATUS_NOT_FOUND 2u
#define VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED 3u
#define VISION_RUNTIME_BACKEND_STATUS_BACKEND_ERROR 4u
#define VISION_RUNTIME_BACKEND_STATUS_INTERNAL 5u

typedef uint32_t VisionRuntimeBackendDataType;
#define VISION_RUNTIME_BACKEND_DATA_TYPE_FLOAT32 1u

typedef struct VisionRuntimeStringView {
	/* UTF-8 text. The view is borrowed for the duration of the API call. */
	const char* data;
	size_t size;
} VisionRuntimeStringView;

typedef struct VisionRuntimeErrorBuffer {
	/* Writable bytes available to the plugin; the runtime owns termination. */
	char* data;
	size_t capacity;
} VisionRuntimeErrorBuffer;

typedef struct VisionRuntimeBackendCreateOptions {
	uint32_t structSize;
	VisionRuntimeStringView artifactPath;
	VisionRuntimeStringView device;
	VisionRuntimeStringView optionsJson;
	VisionRuntimeStringView artifactKind;
	VisionRuntimeStringView inputName;
	VisionRuntimeStringView outputName;
} VisionRuntimeBackendCreateOptions;

typedef struct VisionRuntimeTensorView {
	VisionRuntimeStringView name;
	VisionRuntimeBackendDataType dataType;
	const int64_t* dimensions;
	size_t rank;
	const void* data;
	size_t byteSize;
} VisionRuntimeTensorView;

typedef VisionRuntimeBackendStatus (*VisionRuntimeAllocateOutput)(
	void* context,
	VisionRuntimeStringView name,
	VisionRuntimeBackendDataType dataType,
	const int64_t* dimensions,
	size_t rank,
	size_t byteSize,
	void** data);

typedef struct VisionRuntimeOutputAllocator {
	void* context;
	VisionRuntimeAllocateOutput allocate;
} VisionRuntimeOutputAllocator;

typedef struct VisionRuntimeBackendApi {
	uint32_t structSize;
	uint32_t abiMajor;
	uint32_t abiMinor;
	VisionRuntimeStringView backendId;
	VisionRuntimeBackendStatus (*create)(
		const VisionRuntimeBackendCreateOptions* options,
		void** backend,
		VisionRuntimeErrorBuffer error);
	void (*destroy)(void* backend);
	VisionRuntimeBackendStatus (*infer)(
		void* backend,
		const VisionRuntimeTensorView* inputs,
		size_t inputCount,
		VisionRuntimeOutputAllocator outputs,
		VisionRuntimeErrorBuffer error);
} VisionRuntimeBackendApi;

typedef const VisionRuntimeBackendApi* (*VisionRuntimeBackendQueryFunction)(void);

#define VISION_RUNTIME_BACKEND_CREATE_OPTIONS_V1_SIZE \
	(offsetof(VisionRuntimeBackendCreateOptions, outputName) + \
	 sizeof(((VisionRuntimeBackendCreateOptions*)0)->outputName))
#define VISION_RUNTIME_BACKEND_API_V1_SIZE \
	(offsetof(VisionRuntimeBackendApi, infer) + \
	 sizeof(((VisionRuntimeBackendApi*)0)->infer))

VISION_RUNTIME_BACKEND_EXPORT const VisionRuntimeBackendApi*
	visionRuntimeBackendQuery(void);

#ifdef __cplusplus
}
#endif
