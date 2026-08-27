/**
 * @file cameraTypes.hpp
 * @author epifnne
 * @date 2026-08-25
 * @brief Defines vendor-neutral industrial camera configuration and capabilities.
 */

#pragma once

#include "vision/frameSpec.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace visionRuntime::camera {

enum class CameraTransport {
	GigE,
	Usb
};

enum class AcquisitionMode {
	Continuous,
	SoftwareTrigger
};

struct CameraAcquisitionOptions {
	AcquisitionMode mode = AcquisitionMode::Continuous;
	std::optional<double> frameRate;

	[[nodiscard]] bool isValid() const noexcept {
		switch (mode) {
		case AcquisitionMode::Continuous:
			return !frameRate || (std::isfinite(*frameRate) && *frameRate > 0.0);
		case AcquisitionMode::SoftwareTrigger:
			return !frameRate;
		}
		return false;
	}
};

struct CameraDeviceInfo {
	std::string serialNumber;
	std::string modelName;
	std::string userDefinedName;
	CameraTransport transport = CameraTransport::GigE;
	std::optional<std::string> ipAddress;
};

struct CameraDeviceOptions {
	std::string serialNumber;
	std::string ipAddress;
	vision::PixelFormat pixelFormat = vision::PixelFormat::Gray8;
	std::optional<double> exposureMicroseconds;
	std::optional<double> gain;
	std::size_t maxFramesInFlight = 3;
	std::chrono::milliseconds frameTimeout{100};
};

struct CameraCapabilities {
	std::vector<vision::PixelFormat> pixelFormats;
	bool supportsSoftwareTrigger = false;
	bool supportsHardwareTimestamp = false;
	bool supportsSdkBufferLease = false;
	bool supportsUserBuffers = false;
};

} // namespace visionRuntime::camera