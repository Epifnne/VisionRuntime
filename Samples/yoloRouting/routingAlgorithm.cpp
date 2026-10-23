#include "routingAlgorithm.hpp"

#include "logs/logger.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace visionSample::yoloRouting {
using namespace visionRuntime;

namespace {

template<typename T>
core::Result<T> failure(core::StatusCode code, std::string message) {
	auto status = core::Status::error(code, std::move(message));
	logs::report(status);
	return core::Result<T>::failure(std::move(status));
}

core::Result<cv::Mat> copyImage(const vision::Frame& frame) {
	if (!frame || !frame.buffer().isHostAccessible() ||
		frame.width() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
		frame.height() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		return failure<cv::Mat>(core::StatusCode::InvalidArgument,
			"routing requires a nonempty host image with OpenCV-compatible dimensions");
	}

	int type;
	switch (frame.pixelFormat()) {
	case vision::PixelFormat::Gray8: type = CV_8UC1; break;
	case vision::PixelFormat::Bgr8: type = CV_8UC3; break;
	case vision::PixelFormat::Bgra8: type = CV_8UC4; break;
	default:
		return failure<cv::Mat>(core::StatusCode::Unsupported,
			"routing supports only Gray8, Bgr8 and Bgra8 input");
	}
	cv::Mat image(static_cast<int>(frame.height()), static_cast<int>(frame.width()), type);
	const auto* source = static_cast<const unsigned char*>(frame.data());
	for (int row = 0; row < image.rows; ++row) {
		std::memcpy(image.ptr(row),
			source + static_cast<std::size_t>(row) * frame.rowStride(),
			static_cast<std::size_t>(image.cols) * image.elemSize());
	}
	return core::Result<cv::Mat>::success(std::move(image));
}

core::Result<cv::Rect> clipBox(const cv::Rect& box, const cv::Mat& image) {
	if (box.width <= 0 || box.height <= 0) {
		return failure<cv::Rect>(core::StatusCode::InvalidArgument,
			"detection box dimensions must be positive");
	}
	const auto left = std::max<std::int64_t>(0, box.x);
	const auto top = std::max<std::int64_t>(0, box.y);
	const auto right = std::min<std::int64_t>(image.cols,
		static_cast<std::int64_t>(box.x) + box.width);
	const auto bottom = std::min<std::int64_t>(image.rows,
		static_cast<std::int64_t>(box.y) + box.height);
	if (right <= left || bottom <= top) {
		return failure<cv::Rect>(core::StatusCode::InvalidArgument,
			"detection box does not intersect the image");
	}
	return core::Result<cv::Rect>::success(cv::Rect(
		static_cast<int>(left), static_cast<int>(top),
		static_cast<int>(right - left), static_cast<int>(bottom - top)));
}

void annotate(cv::Mat& image, const Detection& detection, const cv::Scalar& color) {
	cv::rectangle(image, detection.box, color, 2);
	cv::putText(image, detection.label,
		{detection.box.x, std::max(12, detection.box.y - 4)},
		cv::FONT_HERSHEY_SIMPLEX, 0.45, color, 1);
}

} // namespace

core::Result<std::vector<Detection>> mockYoloDetect(const cv::Mat& image) {
	if (image.empty()) {
		return failure<std::vector<Detection>>(core::StatusCode::InvalidArgument,
			"mock YOLO requires a nonempty image");
	}
	const int w = image.cols;
	const int h = image.rows;
	return core::Result<std::vector<Detection>>::success({
		{BoxKind::Object, {w / 10, h / 10, std::max(1, w / 2),
			std::max(1, static_cast<int>(h * 0.6))}, "mock-product", 0.95F},
		{BoxKind::Defect, {w / 4, h / 3, std::max(1, w / 8),
			std::max(1, h / 10)}, "mock-scratch", 0.88F},
		{BoxKind::Text, {w / 10, static_cast<int>(h * 0.75),
			std::max(1, static_cast<int>(w * 0.6)), std::max(1, h / 8)},
			"mock-text", 0.91F},
	});
}

core::Result<OcrText> recognizeText(const cv::Mat&) {
	return core::Result<OcrText>::failure(core::Status::error(
		core::StatusCode::Unsupported, "OCR placeholder: recognition is not implemented"));
}

RoutingAlgorithm::RoutingAlgorithm(
	Detector detector, OcrRecognizer ocr, bool simulatedDetection)
	: detector_(std::move(detector)), ocr_(std::move(ocr)),
	  simulatedDetection_(simulatedDetection) {}

core::Result<InspectionResult> RoutingAlgorithm::process(
	const pipeline::PipelinePacket& packet) {
	if (!detector_ || !ocr_) {
		return failure<InspectionResult>(core::StatusCode::InvalidState,
			"routing requires detector and OCR functions");
	}
	const auto* frame = packet.businessFrame();
	if (!frame) {
		frame = packet.cameraFrame();
	}
	if (!frame) {
		return failure<InspectionResult>(core::StatusCode::InvalidState,
			"routing packet no longer owns an image");
	}
	auto copied = copyImage(*frame);
	if (!copied) {
		return core::Result<InspectionResult>::failure(copied.status());
	}
	auto image = std::move(copied).value();
	auto detected = detector_(image);
	if (!detected) {
		logs::report(detected.status());
		return core::Result<InspectionResult>::failure(
			detected.status().withContext("detector"));
	}

	InspectionResult result;
	result.simulatedDetection = simulatedDetection_;
	if (image.channels() == 1) {
		cv::cvtColor(image, result.annotatedImage, cv::COLOR_GRAY2BGR);
	} else if (image.channels() == 4) {
		cv::cvtColor(image, result.annotatedImage, cv::COLOR_BGRA2BGR);
	} else {
		result.annotatedImage = image.clone();
	}

	for (auto detection : detected.value()) {
		if (!std::isfinite(detection.confidence) || detection.confidence < 0.0F ||
			detection.confidence > 1.0F || detection.label.empty()) {
			return failure<InspectionResult>(core::StatusCode::InvalidArgument,
				"detection requires a label and confidence in [0, 1]");
		}
		auto clipped = clipBox(detection.box, image);
		if (!clipped) {
			return core::Result<InspectionResult>::failure(clipped.status());
		}
		detection.box = clipped.value();
		switch (detection.kind) {
		case BoxKind::Object:
			result.objects.push_back(detection);
			annotate(result.annotatedImage, detection, {0, 255, 0});
			break;
		case BoxKind::Defect:
			result.defects.push_back(detection);
			annotate(result.annotatedImage, detection, {0, 0, 255});
			break;
		case BoxKind::Text: {
			auto recognized = ocr_(image(detection.box));
			if (!recognized) {
				logs::report(recognized.status().withContext("OCR region"));
			}
			result.texts.push_back({detection, std::move(recognized)});
			annotate(result.annotatedImage, detection, {255, 0, 0});
			break;
		}
		default:
			return failure<InspectionResult>(core::StatusCode::InvalidArgument,
				"unknown detection box kind");
		}
	}
	return core::Result<InspectionResult>::success(std::move(result));
}

} // namespace visionSample::yoloRouting
