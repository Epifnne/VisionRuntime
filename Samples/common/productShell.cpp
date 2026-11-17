#include "productShell.hpp"
#include "sampleRuntime.hpp"
#include "profileSupport.hpp"

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
	options.deployment = deploymentOptions(profile);
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
		const auto duration = document.value("durationMilliseconds", nlohmann::json(0));
		if (!duration.is_number_integer() || duration.get<std::int64_t>() < 0) {
			return fail<Session>("durationMilliseconds must be a nonnegative integer");
		}
		if (duration.get<std::int64_t>() > 0) {
			options.duration = std::chrono::milliseconds(duration.get<std::int64_t>());
		}
		if (document.contains("backend")) {
			options.pipeline.backendStreams = document.at("backend").value("streams", 1);
			options.pipeline.dynamicBatch = document.at("backend").value("dynamicBatch", false);
		}
	} catch (const nlohmann::json::exception& exception) {
		return fail<Session>(std::string("sample backend configuration: ") + exception.what());
	}
	for (const auto& source : profile.sources) {
		if (source.type == "camera" && source.camera.pixelFormat != "gray8") {
			return fail<Session>("sample camera input requires pixelFormat gray8");
		}
		auto config = sourceConfig(source);
		if (!config) { return core::Result<Session>::failure(config.status()); }
		options.sources.push_back(std::move(config).value());
	}
	auto built = createSampleRuntime(std::move(options));
	if (!built) {
		return core::Result<Session>::failure(built.status());
	}
	return core::Result<Session>::success(
		visionService::session::adoptRuntime(std::move(built).value()));
}

} // namespace visionSample
