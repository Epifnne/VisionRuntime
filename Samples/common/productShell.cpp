#include "productShell.hpp"
#include "sampleRuntime.hpp"

namespace visionSample {
using namespace visionRuntime;

core::Result<std::unique_ptr<visionService::session::IInspectionSession>>
createProductRuntime(const visionService::profile::ProductProfile& profile,
	visionService::endpoints::EndpointRegistry&) {
	using Session = std::unique_ptr<visionService::session::IInspectionSession>;
	if (!profile.runtimeProduct.empty() && profile.runtimeProduct != VISION_PRODUCT_ID) {
		return fail<Session>("profile belongs to another product runtime; use its matching Shell");
	}
	RuntimeOptions options;
	options.productDocumentJson = profile.documentJson;
	options.modelPackage = profile.model.packagePath;
	options.deployment.backend = {
		.pluginDirectory = profile.model.pluginDirectory,
		.id = profile.model.backendId, .device = profile.model.device};
	options.deployment.executor.queueCapacity = profile.pipeline.queueCapacity;
	options.deployment.executor.stageQueueCapacity = profile.pipeline.stageQueueCapacity;
	options.deployment.executor.taskTimeout = profile.pipeline.taskTimeout;
	options.deployment.executor.queueFullPolicy = profile.pipeline.queueFullPolicy == "block"
		? config::QueueFullPolicy::Block : config::QueueFullPolicy::Drop;
	options.deployment.executor.performancePolicy = config::PerformancePolicy::Serial;
	options.faultPolicy = profile.pipeline.faultPolicy == "stopSession"
		? executor::FaultPolicy::StopSession : executor::FaultPolicy::ReportAndContinue;
	options.pipeline.threshold = profile.pipeline.threshold;
	options.pipeline.resizeShortSide = profile.pipeline.resizeShortSide;
	options.pipeline.cropWidth = profile.pipeline.cropWidth;
	options.pipeline.cropHeight = profile.pipeline.cropHeight;
	options.pipeline.mean = profile.pipeline.mean;
	options.pipeline.standardDeviation = profile.pipeline.standardDeviation;
	options.pipeline.imageChannels = profile.pipeline.mean.size();
	options.pipeline.maxBatchSize = profile.pipeline.maxBatchSize > 0
		? profile.pipeline.maxBatchSize : profile.sources.size();
	options.pipeline.flushTimeout = profile.pipeline.flushTimeout;
	try {
		const auto document = nlohmann::json::parse(profile.documentJson);
		if (document.contains("backend")) {
			options.pipeline.backendStreams = document.at("backend").value("streams", 1);
			options.pipeline.dynamicBatch = document.at("backend").value("dynamicBatch", false);
		}
	} catch (const nlohmann::json::exception& exception) {
		return fail<Session>(std::string("sample backend configuration: ") + exception.what());
	}
	for (const auto& source : profile.sources) {
		if (source.type == "directory") {
			camera::FileSourceOptions files;
			files.directory = source.directory.directory;
			files.extensions = source.directory.extensions;
			files.frameInterval = source.directory.frameInterval;
			files.loop = source.directory.loop;
			files.recursive = source.directory.recursive;
			options.sources.push_back(camera::FileFrameSourceConfig{std::move(files)});
			continue;
		}
		// These samples assemble single-channel anomaly pipelines.
		if (source.camera.pixelFormat != "gray8") {
			return fail<Session>("sample camera input requires pixelFormat gray8");
		}
		camera::CameraDeviceOptions device;
		device.serialNumber = source.camera.serialNumber;
		device.ipAddress = source.camera.ipAddress;
		device.vendor = source.camera.vendor;
		device.pixelFormat = vision::PixelFormat::Gray8;
		device.exposureMicroseconds = source.camera.exposureMicroseconds;
		device.gain = source.camera.gain;
		device.maxFramesInFlight = source.camera.maxFramesInFlight;
		if (source.camera.mode == "timedTrigger") {
			options.sources.push_back(camera::TimedCameraSourceConfig{
				.device = std::move(device),
				.source = {.triggerInterval = source.camera.triggerInterval,
					.responseTimeout = source.camera.responseTimeout}});
		} else {
			options.sources.push_back(camera::ContinuousCameraSourceConfig{
				.device = std::move(device),
				.source = {.frameRate = source.camera.frameRate,
					.noFrameTimeout = source.camera.noFrameTimeout}});
		}
	}
	auto built = createSampleRuntime(std::move(options));
	if (!built) {
		return core::Result<Session>::failure(built.status());
	}
	return core::Result<Session>::success(
		visionService::session::adoptRuntime(std::move(built).value()));
}

} // namespace visionSample
