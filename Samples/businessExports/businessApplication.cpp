#include "exports/productEntry.hpp"
#include "businessContract.hpp"

#include "camera/fileSource.hpp"
#include "pipeline/openCvPipelineBuilder.hpp"
#include "runtime/runtimeFactory.hpp"
#include "../common/profileSupport.hpp"
#ifdef VISION_MEASUREMENT_CUDA
#include "cudaThresholdAlgorithm.hpp"
#endif

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

#ifdef VISION_MEASUREMENT_CUDA
class CudaMeasurementAlgorithm final : public pipeline::IOpenCvAlgorithm<Measurement> {
public:
	CudaMeasurementAlgorithm(
		std::unique_ptr<visionSample::openCvCuda::CudaThresholdAlgorithm> algorithm,
		std::shared_ptr<std::atomic<double>> minimum)
		: algorithm_(std::move(algorithm)), minimum_(std::move(minimum)) {}

	core::Result<Measurement> process(const pipeline::PipelinePacket& packet) override {
		auto result = algorithm_->process(packet);
		if (!result) {
			return core::Result<Measurement>::failure(result.status());
		}
		return core::Result<Measurement>::success(Measurement{
			.sequence = result->sequence, .brightFraction = result->brightFraction,
			.accepted = result->brightFraction >= minimum_->load(), .valid = true});
	}

private:
	std::unique_ptr<visionSample::openCvCuda::CudaThresholdAlgorithm> algorithm_;
	std::shared_ptr<std::atomic<double>> minimum_;
};
#endif

} // namespace

core::Result<visionService::ProductApplication> createApplication(
	const visionService::profile::ProductProfile& profile) {
	using Application = visionService::ProductApplication;
	Application application;
	auto cuda = measurementUsesCuda(profile);
	if (!cuda) { return core::Result<Application>::failure(cuda.status()); }
#ifndef VISION_MEASUREMENT_CUDA
	if (cuda.value()) {
		return visionSample::profileError<Application>(
			"CUDA measurement requires VISION_WITH_OPENCV_CUDA=ON; CPU fallback is disabled");
	}
#endif
	const auto& contract = businessContract(cuda.value());
	auto manifest = describeApplication(profile);
	if (!manifest) {
		return core::Result<Application>::failure(manifest.status());
	}
	application.expectedManifest = std::move(manifest).value();
	double configuredMinimum = 100;
#ifdef VISION_MEASUREMENT_CUDA
	int device = 0;
	double threshold = 127;
#endif
	try {
		const auto document = nlohmann::json::parse(profile.documentJson);
		if (cuda.value()) {
			const auto& config = document.at("cuda");
#ifdef VISION_MEASUREMENT_CUDA
			const auto& value = config.at("device");
			if (!value.is_number_integer() || value.get<std::int64_t>() < 0 ||
				value.get<std::int64_t>() > std::numeric_limits<int>::max()) {
				return visionSample::profileError<Application>("cuda.device must be a nonnegative device index");
			}
			device = value.get<int>();
			threshold = config.at("threshold").get<double>();
#endif
			configuredMinimum = config.at("minimumBrightFraction").get<double>();
		} else {
			configuredMinimum = document.value("measurement", nlohmann::json::object())
				.value("minimumBrightness", 100.0);
		}
		if (!std::isfinite(configuredMinimum) || configuredMinimum < 0 ||
			configuredMinimum > (cuda.value() ? 1 : 255)) {
			return visionSample::profileError<Application>(cuda.value()
				? "cuda.minimumBrightFraction must be in [0, 1]"
				: "measurement.minimumBrightness must be in [0, 255]");
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
	std::unique_ptr<pipeline::IOpenCvAlgorithm<Measurement>> algorithm;
#ifdef VISION_MEASUREMENT_CUDA
	if (cuda.value()) {
		auto gpu = visionSample::openCvCuda::CudaThresholdAlgorithm::create(device, threshold, configuredMinimum);
		if (!gpu) { return core::Result<Application>::failure(gpu.status()); }
		algorithm = std::make_unique<CudaMeasurementAlgorithm>(std::move(gpu).value(), minimum);
	} else
#endif
	{
		algorithm = std::make_unique<BrightnessAlgorithm>(minimum);
	}
	auto pipeline = builder.setAlgorithm(std::move(algorithm)).build();
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
