#include "camera/frameSourceFactory.hpp"

#include "camera/continuousCameraSource.hpp"
#include "camera/fileSource.hpp"
#include "camera/hikrobotMvsCameraDevice.hpp"
#include "camera/iCameraDevice.hpp"
#include "camera/timedTriggerSource.hpp"
#include "config/buildProfile.hpp"
#include "core/status.hpp"

#include <type_traits>
#include <utility>
#include <variant>

namespace visionRuntime::camera {
namespace {

template<typename Interface, typename Implementation>
[[nodiscard]] core::Result<std::unique_ptr<Interface>> upcast(
	core::Result<std::unique_ptr<Implementation>> result) {
	if (!result) {
		return core::Result<std::unique_ptr<Interface>>::failure(result.status());
	}
	return core::Result<std::unique_ptr<Interface>>::success(
		std::move(result).value());
}

template<config::CameraSdk Sdk = config::BuildProfile::cameraSdk>
[[nodiscard]] core::Result<std::unique_ptr<ICameraDevice>> createCameraDevice(
	CameraDeviceOptions options) {
	if constexpr (Sdk == config::CameraSdk::HikMvs) {
		return upcast<ICameraDevice>(
			HikrobotMvsCameraDevice::create(std::move(options)));
	} else {
		return core::Result<std::unique_ptr<ICameraDevice>>::failure(
			core::Status::error(core::StatusCode::Unsupported,
				"this Runtime build has no camera SDK"));
	}
}

[[nodiscard]] core::Result<std::unique_ptr<IFrameSource>> createSource(
	const FileFrameSourceConfig& config) {
	return upcast<IFrameSource>(FileSource::create(config.source));
}

[[nodiscard]] core::Result<std::unique_ptr<IFrameSource>> createSource(
	const ContinuousCameraSourceConfig& config) {
	if (!config.source.isValid()) {
		return core::Result<std::unique_ptr<IFrameSource>>::failure(
			core::Status::error(core::StatusCode::InvalidArgument,
				"invalid continuous camera source options"));
	}
	auto device = createCameraDevice(config.device);
	if (!device) {
		return core::Result<std::unique_ptr<IFrameSource>>::failure(device.status());
	}
	return upcast<IFrameSource>(ContinuousCameraSource::create(
		std::move(device).value(), config.source));
}

[[nodiscard]] core::Result<std::unique_ptr<IFrameSource>> createSource(
	const TimedCameraSourceConfig& config) {
	if (!config.source.isValid()) {
		return core::Result<std::unique_ptr<IFrameSource>>::failure(
			core::Status::error(core::StatusCode::InvalidArgument,
				"invalid timed trigger source options"));
	}
	auto device = createCameraDevice(config.device);
	if (!device) {
		return core::Result<std::unique_ptr<IFrameSource>>::failure(device.status());
	}
	return upcast<IFrameSource>(TimedTriggerSource::create(
		std::move(device).value(), config.source));
}

} // namespace

core::Result<std::unique_ptr<IFrameSource>> FrameSourceFactory::create(
	const FrameSourceConfig& config) {
	return std::visit([](const auto& sourceConfig) {
		return createSource(sourceConfig);
	}, config);
}

} // namespace visionRuntime::camera
