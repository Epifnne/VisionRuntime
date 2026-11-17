#include "businessContract.hpp"
#include "routingAlgorithm.hpp"
#include "exports/productEntry.hpp"
#include "../common/profileSupport.hpp"
#include "pipeline/openCvPipelineBuilder.hpp"
#include "runtime/runtimeFactory.hpp"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace visionProduct {
using namespace visionRuntime;

namespace {
class AnnotatingAlgorithm final
	: public pipeline::IOpenCvAlgorithm<visionSample::yoloRouting::InspectionResult> {
public:
	AnnotatingAlgorithm(visionSample::yoloRouting::Detector detector, std::filesystem::path output)
		: algorithm_(std::move(detector)), output_(std::move(output)) {}

	core::Result<visionSample::yoloRouting::InspectionResult> process(
		const pipeline::PipelinePacket& packet) override {
		auto result = algorithm_.process(packet);
		if (!result) { return result; }
		const auto path = output_ / ("frame-" +
			std::to_string(packet.cameraFrame()->metadata().sequenceNumber) + ".png");
		try {
			if (!cv::imwrite(path.string(), result->annotatedImage)) {
				return visionSample::profileError<visionSample::yoloRouting::InspectionResult>(
					"cannot write annotation: " + path.string());
			}
		} catch (const cv::Exception& exception) {
			return visionSample::profileError<visionSample::yoloRouting::InspectionResult>(
				std::string("cannot write annotation: ") + exception.what());
		}
		return result;
	}

private:
	visionSample::yoloRouting::RoutingAlgorithm algorithm_;
	std::filesystem::path output_;
};
}

core::Result<visionService::ProductApplication> createApplication(
	const visionService::profile::ProductProfile& profile) {
	using Application = visionService::ProductApplication;
	using namespace visionSample::yoloRouting;
	auto manifest = describeApplication(profile);
	if (!manifest) { return core::Result<Application>::failure(manifest.status()); }
	std::filesystem::path output;
	double minimumConfidence = 0;
	try {
		const auto document = nlohmann::json::parse(profile.documentJson);
		const auto& routing = document.at("routing");
		output = visionSample::assetPath(profile, routing.at("annotationDirectory").get<std::string>());
		minimumConfidence = routing.value("minimumConfidence", 0.0);
		if (!std::isfinite(minimumConfidence) || minimumConfidence < 0 || minimumConfidence > 1 ||
			routing.at("annotationDirectory").get<std::string>().empty()) {
			return visionSample::profileError<Application>("routing requires an annotationDirectory and confidence in [0, 1]");
		}
		if (profile.sources[0].type == "directory" &&
			std::filesystem::weakly_canonical(output) ==
				std::filesystem::weakly_canonical(profile.sources[0].directory.directory)) {
			return visionSample::profileError<Application>("annotationDirectory must differ from the input directory");
		}
		std::filesystem::create_directories(output);
	} catch (const nlohmann::json::exception& exception) {
		return visionSample::profileError<Application>(std::string("routing configuration: ") + exception.what());
	} catch (const std::filesystem::filesystem_error& exception) {
		return visionSample::profileError<Application>(std::string("annotation directory: ") + exception.what());
	}
	auto source = visionSample::createSource(profile.sources[0]);
	if (!source) { return core::Result<Application>::failure(source.status()); }
	Application application;
	application.expectedManifest = manifest.value();
	auto state = businessContract().result.bind(application.exports, RoutingSummary{});
	if (!state) { return core::Result<Application>::failure(state.status()); }
	auto annotated = businessContract().annotated.bind(application.exports);
	Detector detector = [minimumConfidence](const cv::Mat& image) {
		auto detections = mockYoloDetect(image);
		if (detections) {
			std::erase_if(detections.value(), [minimumConfidence](const auto& detection) {
				return detection.confidence < minimumConfidence;
			});
		}
		return detections;
	};
	pipeline::OpenCvPipelineBuilder<InspectionResult> builder;
	auto built = builder.setAlgorithm(std::make_unique<AnnotatingAlgorithm>(
		std::move(detector), output)).build();
	if (!built) { return core::Result<Application>::failure(built.status()); }
	executor::FrameExecutionOptions<InspectionResult> execution;
	execution.faultPolicy = profile.pipeline.faultPolicy == "stopSession"
		? executor::FaultPolicy::StopSession : executor::FaultPolicy::ReportAndContinue;
	execution.resultObserver = [state = state.value(), annotated](auto, const auto& identity,
		auto, const core::Result<InspectionResult>& result) {
		RoutingSummary summary{.sequence = identity.metadata.sequenceNumber};
		if (result) {
			summary.objects = result->objects.size();
			summary.defects = result->defects.size();
			summary.texts = result->texts.size();
			summary.simulatedDetection = result->simulatedDetection;
			summary.valid = true;
			for (const auto& object : result->objects) {
				std::cout << "classification execution=" << identity.executionId << " label=" << object.label
					<< " confidence=" << object.confidence << " box=" << object.box << '\n';
			}
			for (const auto& defect : result->defects) {
				std::cout << "defect label=" << defect.label << " box=" << defect.box << '\n';
			}
			for (const auto& text : result->texts) {
				std::cout << "ocr box=" << text.detection.box << ' ';
				if (text.recognition) { std::cout << text.recognition->text << '\n'; }
				else { std::cout << text.recognition.status().toString() << '\n'; }
			}
			auto image = std::make_shared<cv::Mat>(result->annotatedImage);
			auto buffer = core::TensorBuffer::share(image, image->data,
				image->total() * image->elemSize(), core::Device::cpu(), core::MemoryKind::Host, false);
			if (buffer) {
				auto frame = vision::Frame::create(std::move(buffer).value(),
					static_cast<std::size_t>(image->cols), static_cast<std::size_t>(image->rows),
					vision::PixelFormat::Bgr8, image->step, identity.metadata);
				if (frame) {
					auto published = annotated->publish(std::move(frame).value());
					if (!published) { summary.error = published.status().toString(); }
				} else { summary.error = frame.status().toString(); }
			} else { summary.error = buffer.status().toString(); }
		} else { summary.error = result.status().toString(); }
		if (!summary.error.empty()) { summary.valid = false; logs::error(summary.error); }
		auto published = state->set(summary);
		if (!published) { logs::report(published.status()); }
	};
	auto runtime = runtime::RuntimeFactory::createRuntime<InspectionResult>(
		std::move(source).value(),
		std::make_unique<pipeline::OpenCvPipeline<InspectionResult>>(std::move(built).value()),
		visionSample::deploymentOptions(profile), std::move(execution));
	if (!runtime) { return core::Result<Application>::failure(runtime.status()); }
	application.session = visionService::session::adoptProductRuntime<InspectionResult>(
		std::move(runtime).value());
	return core::Result<Application>::success(std::move(application));
}

} // namespace visionProduct
