#pragma once

#include "pipeline/iOpenCvAlgorithm.hpp"

#include <opencv2/core.hpp>

#include <functional>
#include <string>
#include <vector>

namespace visionSample::yoloRouting {

enum class BoxKind {
	Object,
	Defect,
	Text
};

struct Detection {
	BoxKind kind;
	cv::Rect box;
	std::string label;
	float confidence;
};

struct OcrText {
	std::string text;
	float confidence;
};

struct TextRegion {
	Detection detection;
	visionRuntime::core::Result<OcrText> recognition;
};

struct InspectionResult {
	bool simulatedDetection = true;
	std::vector<Detection> objects;
	std::vector<Detection> defects;
	std::vector<TextRegion> texts;
	cv::Mat annotatedImage;
};

using Detector = std::function<visionRuntime::core::Result<std::vector<Detection>>(
	const cv::Mat&)>;
using OcrRecognizer = std::function<visionRuntime::core::Result<OcrText>(
	const cv::Mat&)>;

[[nodiscard]] visionRuntime::core::Result<std::vector<Detection>> mockYoloDetect(
	const cv::Mat& image);
[[nodiscard]] visionRuntime::core::Result<OcrText> recognizeText(
	const cv::Mat& roi);

class RoutingAlgorithm final
	: public visionRuntime::pipeline::IOpenCvAlgorithm<InspectionResult> {
public:
	explicit RoutingAlgorithm(
		Detector detector = mockYoloDetect,
		OcrRecognizer ocr = recognizeText,
		bool simulatedDetection = true);

	[[nodiscard]] visionRuntime::core::Result<InspectionResult> process(
		const visionRuntime::pipeline::PipelinePacket& packet) override;

private:
	Detector detector_;
	OcrRecognizer ocr_;
	bool simulatedDetection_;
};

} // namespace visionSample::yoloRouting
