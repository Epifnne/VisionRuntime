/**
 * @file productProfile.hpp
 * @brief Product profile model: the single source of product differences.
 *
 * The profile JSON mirrors deployment.json conventions and carries camera
 * roles, source/pipeline/deployment assembly parameters, the exposed endpoint
 * subset and the UI definition. The UI definition is passed through verbatim;
 * visionService does not interpret it.
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace visionService::profile {

struct DirectorySourceProfile {
	std::filesystem::path directory;
	std::vector<std::string> extensions;
	bool loop = true;
	bool recursive = false;
	std::chrono::milliseconds frameInterval{0};
};

struct CameraSourceProfile {
	std::string serialNumber;
	std::string ipAddress;
	std::string pixelFormat;
	std::optional<double> exposureMicroseconds;
	std::optional<double> gain;
	std::size_t maxFramesInFlight = 6;
	std::string mode;
	std::optional<double> frameRate;
	std::chrono::milliseconds triggerInterval{100};
	std::chrono::milliseconds responseTimeout{1000};
};

struct SourceProfile {
	std::string id;
	std::string type;
	std::string role;
	DirectorySourceProfile directory;
	CameraSourceProfile camera;
};

struct ModelProfile {
	std::filesystem::path packagePath;
	std::string backendId;
	std::string device;
	std::filesystem::path pluginDirectory;
};

struct PipelineProfile {
	std::size_t resizeShortSide = 256;
	std::size_t cropWidth = 224;
	std::size_t cropHeight = 224;
	std::vector<float> mean{0.0F};
	std::vector<float> standardDeviation{1.0F};
	float threshold = 0.5F;
	std::size_t maxBatchSize = 0;
	std::chrono::milliseconds flushTimeout{20};
	std::size_t queueCapacity = 16;
	std::size_t stageQueueCapacity = 1;
	std::string queueFullPolicy = "drop";
};

struct EndpointExposure {
	std::string name;
	std::string access;
};

struct ProductProfile {
	std::uint32_t schemaMajor = 1;
	std::uint32_t schemaMinor = 0;
	std::string id;
	std::string name;
	std::vector<SourceProfile> sources;
	ModelProfile model;
	PipelineProfile pipeline;
	std::vector<EndpointExposure> endpoints;
	/// Raw `ui` subtree serialized back to JSON; not interpreted by the service.
	std::string uiJson;
};

} // namespace visionService::profile
