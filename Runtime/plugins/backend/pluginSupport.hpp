#pragma once

#include "backends/backendPluginApi.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace visionRuntime::plugins {

inline std::string_view text(VisionRuntimeStringView value) noexcept {
	return {value.data, value.size};
}

inline VisionRuntimeStringView view(std::string_view value) noexcept {
	return {value.data(), value.size()};
}

class Failure final : public std::runtime_error {
public:
	Failure(VisionRuntimeBackendStatus status, const std::string& message)
		: std::runtime_error(message), status(status) {}
	VisionRuntimeBackendStatus status;
};

inline void require(bool condition, VisionRuntimeBackendStatus status,
	const char* message) {
	if (!condition) {
		throw Failure(status, message);
	}
}

inline void writeError(VisionRuntimeErrorBuffer error, const char* message) noexcept {
	const auto length = std::min(error.capacity, std::strlen(message));
	if (length != 0) {
		std::memcpy(error.data, message, length);
	}
}

template<typename Function>
VisionRuntimeBackendStatus boundary(VisionRuntimeErrorBuffer error,
	Function&& function) noexcept {
	try {
		function();
		return VISION_RUNTIME_BACKEND_STATUS_OK;
	} catch (const Failure& failure) {
		writeError(error, failure.what());
		return failure.status;
	} catch (const nlohmann::json::exception& exception) {
		writeError(error, exception.what());
		return VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT;
	} catch (const std::exception& exception) {
		writeError(error, exception.what());
		return VISION_RUNTIME_BACKEND_STATUS_INTERNAL;
	} catch (...) {
		writeError(error, "unhandled backend exception");
		return VISION_RUNTIME_BACKEND_STATUS_INTERNAL;
	}
}

inline nlohmann::json parseOptions(const VisionRuntimeBackendCreateOptions& options) {
	require(options.structSize >= VISION_RUNTIME_BACKEND_CREATE_OPTIONS_V1_SIZE,
		VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED, "unsupported create options size");
	require(!text(options.inputName).empty() && !text(options.outputName).empty(),
		VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT, "tensor names must not be empty");
	const auto document = nlohmann::json::parse(text(options.optionsJson));
	require(document.is_object(), VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT,
		"backend options must be a JSON object");
	return document;
}

inline std::filesystem::path artifactPath(const VisionRuntimeBackendCreateOptions& options) {
	const auto encoded = text(options.artifactPath);
	return std::filesystem::path(std::u8string(
		reinterpret_cast<const char8_t*>(encoded.data()), encoded.size()));
}

inline std::size_t tensorBytes(const int64_t* dimensions, std::size_t rank) {
	std::size_t bytes = sizeof(float);
	for (std::size_t index = 0; index < rank; ++index) {
		require(dimensions[index] > 0 && static_cast<std::uint64_t>(dimensions[index]) <=
			std::numeric_limits<std::size_t>::max() / bytes,
			VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT, "invalid tensor dimensions");
		bytes *= static_cast<std::size_t>(dimensions[index]);
	}
	return bytes;
}

inline const VisionRuntimeTensorView& singleInput(const VisionRuntimeTensorView* inputs,
	std::size_t count, const std::string& name) {
	require(count == 1, VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED,
		"backend requires exactly one input");
	const auto& input = inputs[0];
	require(text(input.name) == name, VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT,
		"input tensor name does not match the model");
	require(input.dataType == VISION_RUNTIME_BACKEND_DATA_TYPE_FLOAT32,
		VISION_RUNTIME_BACKEND_STATUS_UNSUPPORTED, "backend requires Float32 input");
	require(input.byteSize == tensorBytes(input.dimensions, input.rank),
		VISION_RUNTIME_BACKEND_STATUS_INVALID_ARGUMENT, "input byte size does not match shape");
	return input;
}

inline void* allocateOutput(VisionRuntimeOutputAllocator outputs, const std::string& name,
	const int64_t* dimensions, std::size_t rank, std::size_t bytes) {
	void* data = nullptr;
	const auto status = outputs.allocate(outputs.context, view(name),
		VISION_RUNTIME_BACKEND_DATA_TYPE_FLOAT32, dimensions, rank, bytes, &data);
	require(status == VISION_RUNTIME_BACKEND_STATUS_OK, status, "output allocation failed");
	return data;
}

} // namespace visionRuntime::plugins