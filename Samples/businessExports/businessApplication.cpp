#include "exports/productEntry.hpp"
#include "businessContract.hpp"

#include "camera/fileSource.hpp"
#include "pipeline/openCvPipelineBuilder.hpp"
#include "runtime/runtimeFactory.hpp"
#include "../common/profileSupport.hpp"

#include <opencv2/core.hpp>

#include <atomic>
#include <cmath>
#include <limits>

namespace visionProduct {
namespace {

using namespace visionRuntime;

class BrightnessAlgorithm final : public pipeline::IOpenCvAlgorithm<Measurement> {
public:
	explicit BrightnessAlgorithm(std::shared_ptr<std::atomic<double>> minimum)
		: minimum_(std::move(minimum)) {}

	core::Result<Measurement> process(const pipeline::PipelinePacket& packet) override {
		const auto* frame = packet.cameraFrame();
		if (!frame || frame->pixelFormat() != vision::PixelFormat::Gray8 ||
			frame->width() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
			frame->height() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
			return core::Result<Measurement>::failure(core::Status::error(
				core::StatusCode::Unsupported, "brightness sample requires a Gray8 image"));
		}
		// OpenCV's header constructor needs a mutable pointer; mean() only reads pixels.
		const cv::Mat image(static_cast<int>(frame->height()), static_cast<int>(frame->width()),
			CV_8UC1, const_cast<void*>(frame->data()), frame->rowStride());
		const auto brightness = cv::mean(image)[0];
		return core::Result<Measurement>::success(Measurement{
			.sequence = frame->metadata().sequenceNumber,
			.brightness = brightness, .accepted = brightness >= minimum_->load(), .valid = true});
	}

private:
	std::shared_ptr<std::atomic<double>> minimum_;
};

} // namespace

core::Result<visionService::ProductApplication> createApplication(
	const visionService::profile::ProductProfile& profile) {
	using Application = visionService::ProductApplication;
	Application application;
	const auto& contract = businessContract();
	auto manifest = describeApplication(profile);
	if (!manifest) {
		return core::Result<Application>::failure(manifest.status());
	}
	application.expectedManifest = std::move(manifest).value();
	double configuredMinimum = 100;
	try {
		const auto document = nlohmann::json::parse(profile.documentJson);
		configuredMinimum = document.value("measurement", nlohmann::json::object())
			.value("minimumBrightness", 100.0);
		if (!std::isfinite(configuredMinimum) || configuredMinimum < 0 || configuredMinimum > 255) {
			return visionSample::profileError<Application>("measurement.minimumBrightness must be in [0, 255]");
		}
	} catch (const nlohmann::json::exception& exception) {
		return visionSample::profileError<Application>(std::string("measurement configuration: ") + exception.what());
	}
	auto minimum = std::make_shared<std::atomic<double>>(configuredMinimum);
	auto parameter = contract.minimum.bind(application.exports,
		[minimum] {
			return core::Result<double>::success(minimum->load());
		},
		[minimum](const double& value) {
			minimum->store(value);
			return core::Result<void>::success();
		});
	if (!parameter) {
		return core::Result<Application>::failure(parameter.status());
	}
	auto state = contract.result.bind(application.exports, Measurement{});
	if (!state) {
		return core::Result<Application>::failure(state.status());
	}
	auto command = contract.reset.bind(application.exports,
		[state = state.value()] { return state->set(Measurement{}); });
	if (!command) {
		return core::Result<Application>::failure(command.status());
	}

	auto source = visionSample::createSource(profile.sources.front());
	if (!source) {
		return core::Result<Application>::failure(source.status());
	}
	pipeline::OpenCvPipelineBuilder<Measurement> builder;
	auto pipeline = builder.setAlgorithm(std::make_unique<BrightnessAlgorithm>(minimum)).build();
	if (!pipeline) {
		return core::Result<Application>::failure(pipeline.status());
	}
	executor::FrameExecutionOptions<Measurement> execution;
	execution.faultPolicy = profile.pipeline.faultPolicy == "stopSession"
		? executor::FaultPolicy::StopSession : executor::FaultPolicy::ReportAndContinue;
	execution.resultObserver = [state = state.value()](
		auto, const auto& identity, auto, const core::Result<Measurement>& result) {
		auto published = state->set(result ? result.value() : Measurement{
			.sequence = identity.metadata.sequenceNumber, .error = result.status().toString()});
		if (!published) {
			logs::report(published.status());
		}
	};
	auto deployment = visionSample::deploymentOptions(profile);
	auto runtime = runtime::RuntimeFactory::createRuntime<Measurement>(
		std::move(source).value(),
		std::make_unique<pipeline::OpenCvPipeline<Measurement>>(std::move(pipeline).value()),
		deployment, std::move(execution));
	if (!runtime) {
		return core::Result<Application>::failure(runtime.status());
	}
	application.session = visionService::session::adoptProductRuntime<Measurement>(
		std::move(runtime).value());
	return core::Result<Application>::success(std::move(application));
}

} // namespace visionProduct
