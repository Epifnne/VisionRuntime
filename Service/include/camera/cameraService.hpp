/**
 * @file cameraService.hpp
 * @brief Camera enumeration and runtime parameter endpoints.
 */

#pragma once

#include "camera/iCameraDevice.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace visionService {

namespace core = visionRuntime::core;

namespace endpoints {
class EndpointRegistry;
}

namespace camera {

class CameraService {
public:
	explicit CameraService(endpoints::EndpointRegistry& registry) noexcept;

	[[nodiscard]] core::Result<std::vector<visionRuntime::camera::CameraDeviceInfo>>
	enumerate() const;

	/**
	 * Registers Parameter/Command endpoints for one camera source slot.
	 * device may be null for directory sources; the endpoints then fail with
	 * InvalidState when used.
	 */
	[[nodiscard]] core::Result<void> registerCameraEndpoints(
		std::size_t sourceIndex,
		std::string sourceId,
		visionRuntime::camera::ICameraDevice* device);

private:
	struct DeviceEntry {
		visionRuntime::camera::ICameraDevice* device = nullptr;
	};

	endpoints::EndpointRegistry& registry_;
	mutable std::mutex mutex_;
	std::unordered_map<std::string, DeviceEntry> devices_;
};

} // namespace camera
} // namespace visionService
