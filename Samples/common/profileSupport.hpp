#pragma once

#include "profile/productProfile.hpp"
#include "camera/frameSourceFactory.hpp"
#include "config/deploymentConfig.hpp"
#include "logs/logger.hpp"

#include <nlohmann/json.hpp>

namespace visionSample {

template<typename T>
visionRuntime::core::Result<T> profileError(std::string message) {
	auto status = visionRuntime::core::Status::error(
		visionRuntime::core::StatusCode::InvalidArgument, std::move(message));
	visionRuntime::logs::report(status);
	return visionRuntime::core::Result<T>::failure(std::move(status));
}

inline visionRuntime::core::Result<visionRuntime::camera::CameraDeviceOptions>
cameraOptions(const visionService::profile::SourceProfile& source) {
	using namespace visionRuntime;
	camera::CameraDeviceOptions options;
	const auto& format = source.camera.pixelFormat;
	if (format == "gray8") { options.pixelFormat = vision::PixelFormat::Gray8; }
	else if (format == "bgr8") { options.pixelFormat = vision::PixelFormat::Bgr8; }
	else if (format == "bgra8") { options.pixelFormat = vision::PixelFormat::Bgra8; }
	else { return profileError<camera::CameraDeviceOptions>("sample requires gray8, bgr8 or bgra8"); }
	options.serialNumber = source.camera.serialNumber;
	options.ipAddress = source.camera.ipAddress;
	options.vendor = source.camera.vendor;
	options.exposureMicroseconds = source.camera.exposureMicroseconds;
	options.gain = source.camera.gain;
	options.maxFramesInFlight = source.camera.maxFramesInFlight;
	return core::Result<camera::CameraDeviceOptions>::success(std::move(options));
}

inline visionRuntime::core::Result<visionRuntime::camera::FrameSourceConfig>
sourceConfig(const visionService::profile::SourceProfile& source) {
	using namespace visionRuntime;
	using Config = camera::FrameSourceConfig;
	if (source.type == "directory") {
		return core::Result<Config>::success(camera::FileFrameSourceConfig{{
			.directory = source.directory.directory,
			.extensions = source.directory.extensions,
			.frameInterval = source.directory.frameInterval,
			.recursive = source.directory.recursive,
			.loop = source.directory.loop}});
	}
	if (source.type != "camera" || source.camera.mode == "eventTrigger") {
		return profileError<Config>("source requires a directory, continuous or timedTrigger camera");
	}
	auto device = cameraOptions(source);
	if (!device) { return core::Result<Config>::failure(device.status()); }
	if (source.camera.mode == "timedTrigger") {
		return core::Result<Config>::success(camera::TimedCameraSourceConfig{
			.device = std::move(device).value(),
			.source = {.triggerInterval = source.camera.triggerInterval,
				.responseTimeout = source.camera.responseTimeout}});
	}
	return core::Result<Config>::success(camera::ContinuousCameraSourceConfig{
		.device = std::move(device).value(),
		.source = {.frameRate = source.camera.frameRate,
			.noFrameTimeout = source.camera.noFrameTimeout}});
}

inline visionRuntime::core::Result<std::unique_ptr<visionRuntime::camera::IFrameSource>>
createSource(const visionService::profile::SourceProfile& profile) {
	auto config = sourceConfig(profile);
	if (!config) {
		return visionRuntime::core::Result<std::unique_ptr<visionRuntime::camera::IFrameSource>>::failure(
			config.status());
	}
	return visionRuntime::camera::FrameSourceFactory::create(config.value());
}

inline visionRuntime::config::DeploymentConfig deploymentOptions(
	const visionService::profile::ProductProfile& profile) {
	using namespace visionRuntime;
	config::DeploymentConfig deployment;
	deployment.backend = {.pluginDirectory = profile.model.pluginDirectory,
		.id = profile.model.backendId, .device = profile.model.device};
	deployment.executor.performancePolicy = config::PerformancePolicy::Serial;
	deployment.executor.queueCapacity = profile.pipeline.queueCapacity;
	deployment.executor.stageQueueCapacity = profile.pipeline.stageQueueCapacity;
	deployment.executor.taskTimeout = profile.pipeline.taskTimeout;
	deployment.executor.queueFullPolicy = profile.pipeline.queueFullPolicy == "block"
		? config::QueueFullPolicy::Block : config::QueueFullPolicy::Drop;
	return deployment;
}

inline std::filesystem::path assetPath(
	const visionService::profile::ProductProfile& profile, const std::string& text) {
	std::filesystem::path path(std::u8string(
		reinterpret_cast<const char8_t*>(text.data()), text.size()));
	return (path.is_relative() ? profile.profileDirectory / path : path).lexically_normal();
}

} // namespace visionSample
