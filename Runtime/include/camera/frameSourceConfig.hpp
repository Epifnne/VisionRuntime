#pragma once

#include "camera/cameraSourceOptions.hpp"
#include "camera/cameraTypes.hpp"

#include <variant>

namespace visionRuntime::camera {

struct FileFrameSourceConfig {
	FileSourceOptions source;
};

struct ContinuousCameraSourceConfig {
	CameraDeviceOptions device;
	ContinuousCameraSourceOptions source;
};

struct TimedCameraSourceConfig {
	CameraDeviceOptions device;
	TimedTriggerSourceOptions source;
};

using FrameSourceConfig = std::variant<
	FileFrameSourceConfig,
	ContinuousCameraSourceConfig,
	TimedCameraSourceConfig>;

} // namespace visionRuntime::camera
