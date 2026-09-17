#include "camera/cameraService.hpp"

#include "camera/hikrobotMvsCameraDevice.hpp"
#include "config/buildProfile.hpp"
#include "endpoints/endpointRegistry.hpp"

#include <cmath>
#include <utility>

namespace visionService::camera {

namespace {

[[nodiscard]] core::Result<void> failure(
	core::StatusCode code, std::string message) {
	return core::Result<void>::failure(
		core::Status::error(code, std::move(message)));
}

[[nodiscard]] core::Result<endpoints::ParameterValue> failureValue(
	core::StatusCode code, std::string message) {
	return core::Result<endpoints::ParameterValue>::failure(
		core::Status::error(code, std::move(message)));
}

[[nodiscard]] std::string parameterName(
	const std::string& sourceId, const char* parameter) {
	return "camera." + sourceId + "." + parameter;
}

} // namespace

CameraService::CameraService(endpoints::EndpointRegistry& registry) noexcept
	: registry_(registry) {}

core::Result<std::vector<visionRuntime::camera::CameraDeviceInfo>>
CameraService::enumerate() const {
	if constexpr (visionRuntime::config::BuildProfile::cameraSdk ==
		visionRuntime::config::CameraSdk::HikMvs) {
		return visionRuntime::camera::HikrobotMvsCameraDevice::enumerate();
	} else {
		return core::Result<
			std::vector<visionRuntime::camera::CameraDeviceInfo>>::failure(
			core::Status::error(core::StatusCode::Unsupported,
				"this build has no camera SDK"));
	}
}

core::Result<void> CameraService::registerCameraEndpoints(
	std::size_t sourceIndex,
	std::string sourceId,
	visionRuntime::camera::ICameraDevice* device) {
	static_cast<void>(sourceIndex);
	const auto exposureName = parameterName(sourceId, "exposureMicroseconds");
	const auto gainName = parameterName(sourceId, "gain");
	const auto triggerName = parameterName(sourceId, "softwareTrigger");

	auto registered = registry_.registerParameter({
		.descriptor = {
			.name = exposureName,
			.description = "camera exposure in microseconds",
			.type = endpoints::ParameterType::Decimal,
			.writable = true,
			.accessLevel = endpoints::AccessLevel::Engineer,
			.minimum = 1.0,
			.maximum = std::nullopt,
		},
		.read = [this, sourceId] {
			std::lock_guard lock(mutex_);
			const auto found = devices_.find(sourceId);
			if (found == devices_.end() || found->second.device == nullptr) {
				return failureValue(core::StatusCode::InvalidState,
					"camera device is not attached: " + sourceId);
			}
			return failureValue(core::StatusCode::Unsupported,
				"camera exposure readback is not available: " + sourceId);
		},
		.write = [this, sourceId](const endpoints::ParameterValue& value) {
			const auto exposure = std::get_if<double>(&value);
			if (exposure == nullptr || !std::isfinite(*exposure) ||
				*exposure <= 0.0) {
				return failure(core::StatusCode::InvalidArgument,
					"exposure must be a positive finite value");
			}
			std::lock_guard lock(mutex_);
			const auto found = devices_.find(sourceId);
			if (found == devices_.end() || found->second.device == nullptr) {
				return failure(core::StatusCode::InvalidState,
					"camera device is not attached: " + sourceId);
			}
			return found->second.device->setExposureMicroseconds(*exposure);
		},
	});
	if (!registered) {
		return registered;
	}

	registered = registry_.registerParameter({
		.descriptor = {
			.name = gainName,
			.description = "camera analog gain",
			.type = endpoints::ParameterType::Decimal,
			.writable = true,
			.accessLevel = endpoints::AccessLevel::Engineer,
			.minimum = 0.0,
			.maximum = std::nullopt,
		},
		.read = [this, sourceId] {
			std::lock_guard lock(mutex_);
			const auto found = devices_.find(sourceId);
			if (found == devices_.end() || found->second.device == nullptr) {
				return failureValue(core::StatusCode::InvalidState,
					"camera device is not attached: " + sourceId);
			}
			return failureValue(core::StatusCode::Unsupported,
				"camera gain readback is not available: " + sourceId);
		},
		.write = [this, sourceId](const endpoints::ParameterValue& value) {
			const auto gain = std::get_if<double>(&value);
			if (gain == nullptr || !std::isfinite(*gain) || *gain < 0.0) {
				return failure(core::StatusCode::InvalidArgument,
					"gain must be a non-negative finite value");
			}
			std::lock_guard lock(mutex_);
			const auto found = devices_.find(sourceId);
			if (found == devices_.end() || found->second.device == nullptr) {
				return failure(core::StatusCode::InvalidState,
					"camera device is not attached: " + sourceId);
			}
			return found->second.device->setGain(*gain);
		},
	});
	if (!registered) {
		return registered;
	}

	registered = registry_.registerCommand({
		.name = triggerName,
		.description = "issue one software trigger",
		.accessLevel = endpoints::AccessLevel::Operator,
		.invoke = [this, sourceId] {
			std::lock_guard lock(mutex_);
			const auto found = devices_.find(sourceId);
			if (found == devices_.end() || found->second.device == nullptr) {
				return failure(core::StatusCode::InvalidState,
					"camera device is not attached: " + sourceId);
			}
			return found->second.device->softwareTrigger();
		},
	});
	if (!registered) {
		return registered;
	}

	std::lock_guard lock(mutex_);
	devices_[sourceId] = DeviceEntry{device};
	return core::Result<void>::success();
}

} // namespace visionService::camera
