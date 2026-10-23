#include "routingAlgorithm.hpp"

#include "executor/serialPipelineExecutor.hpp"
#include "pipeline/openCvPipelineBuilder.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace visionSample::yoloRouting {
namespace {
using namespace visionRuntime;

vision::Frame makeFrame(const cv::Mat& image,
	vision::PixelFormat format = vision::PixelFormat::Bgr8) {
	auto owner = std::make_shared<cv::Mat>(image);
	const auto capacity = (static_cast<std::size_t>(image.rows) - 1) * image.step[0] +
		static_cast<std::size_t>(image.cols) * image.elemSize();
	auto buffer = core::TensorBuffer::share(
		owner, owner->data, capacity, core::Device::cpu(), core::MemoryKind::Host, false);
	if (!buffer) {
		throw std::runtime_error(buffer.status().toString());
	}
	auto frame = vision::Frame::create(std::move(buffer).value(),
		static_cast<std::size_t>(image.cols), static_cast<std::size_t>(image.rows),
		format, image.step[0]);
	if (!frame) {
		throw std::runtime_error(frame.status().toString());
	}
	return std::move(frame).value();
}

Detector detections(std::vector<Detection> boxes) {
	return [boxes = std::move(boxes)](const cv::Mat&) {
		return core::Result<std::vector<Detection>>::success(boxes);
	};
}

TEST(RoutingAlgorithm, RoutesMockBoxesAndKeepsOriginalUnmodified) {
	cv::Mat image(100, 200, CV_8UC3, cv::Scalar(30, 40, 50));
	auto original = image.clone();
	core::Result<InspectionResult> result = [&] {
		pipeline::PipelinePacket packet(makeFrame(image));
		RoutingAlgorithm algorithm;
		return algorithm.process(packet);
	}();
	ASSERT_TRUE(result) << result.status().toString();
	EXPECT_TRUE(result.value().simulatedDetection);
	ASSERT_EQ(result.value().objects.size(), 1U);
	ASSERT_EQ(result.value().defects.size(), 1U);
	ASSERT_EQ(result.value().texts.size(), 1U);
	EXPECT_EQ(result.value().objects[0].label, "mock-product");
	EXPECT_EQ(result.value().defects[0].label, "mock-scratch");
	const auto& text = result.value().texts[0];
	ASSERT_FALSE(text.recognition);
	EXPECT_EQ(text.recognition.status().code(), core::StatusCode::Unsupported);
	EXPECT_EQ(cv::norm(original, image, cv::NORM_INF), 0);
	ASSERT_EQ(result.value().annotatedImage.size(), image.size());
	const auto defect = result.value().defects[0].box;
	EXPECT_EQ(result.value().annotatedImage.at<cv::Vec3b>(defect.y, defect.x),
		cv::Vec3b(0, 0, 255));
}

TEST(RoutingAlgorithm, CropsOriginalRoiAndClipsToImageBounds) {
	cv::Mat image(30, 40, CV_8UC3, cv::Scalar(7, 8, 9));
	bool called = false;
	RoutingAlgorithm algorithm(
		detections({{BoxKind::Text, {-5, 20, 25, 20}, "text", 0.9F}}),
		[&](const cv::Mat& roi) {
			called = true;
			EXPECT_EQ(roi.size(), cv::Size(20, 10));
			EXPECT_EQ(roi.at<cv::Vec3b>(0, 0), cv::Vec3b(7, 8, 9));
			return core::Result<OcrText>::success({"ABC123", 0.98F});
		}, false);
	pipeline::PipelinePacket packet(makeFrame(image));
	auto result = algorithm.process(packet);
	ASSERT_TRUE(result);
	EXPECT_TRUE(called);
	EXPECT_FALSE(result.value().simulatedDetection);
	const auto& text = result.value().texts.at(0);
	EXPECT_EQ(text.detection.box, cv::Rect(0, 20, 20, 10));
	ASSERT_TRUE(text.recognition);
	EXPECT_EQ(text.recognition.value().text, "ABC123");
}

TEST(RoutingAlgorithm, SupportsReadOnlyStridedGrayAndBgraFrames) {
	for (const auto format : {vision::PixelFormat::Gray8, vision::PixelFormat::Bgra8}) {
		const int type = format == vision::PixelFormat::Gray8 ? CV_8UC1 : CV_8UC4;
		cv::Mat backing(40, 80, type, cv::Scalar(30, 40, 50, 255));
		const auto image = backing(cv::Rect(5, 5, 40, 30));
		pipeline::PipelinePacket packet(makeFrame(image, format));
		RoutingAlgorithm algorithm;
		auto result = algorithm.process(packet);
		ASSERT_TRUE(result) << result.status().toString();
		EXPECT_EQ(result.value().annotatedImage.type(), CV_8UC3);
		EXPECT_EQ(result.value().annotatedImage.size(), image.size());
	}
}

TEST(RoutingAlgorithm, HandlesEmptyDetectionsAndOnePixelImage) {
	cv::Mat image(1, 1, CV_8UC3, cv::Scalar(0));
	pipeline::PipelinePacket packet(makeFrame(image));
	RoutingAlgorithm empty(detections({}));
	auto result = empty.process(packet);
	ASSERT_TRUE(result);
	EXPECT_TRUE(result.value().objects.empty());
	EXPECT_TRUE(result.value().defects.empty());
	EXPECT_TRUE(result.value().texts.empty());
	RoutingAlgorithm mock;
	auto tiny = mock.process(packet);
	ASSERT_TRUE(tiny);
	EXPECT_EQ(tiny.value().texts.at(0).detection.box, cv::Rect(0, 0, 1, 1));
}

TEST(RoutingAlgorithm, RejectsInvalidBoxesAndConfidence) {
	cv::Mat image(20, 20, CV_8UC3, cv::Scalar(0));
	pipeline::PipelinePacket packet(makeFrame(image));
	const std::vector<Detection> invalid{
		{BoxKind::Object, {0, 0, 0, 5}, "product", 0.9F},
		{BoxKind::Text, {20, 0, 5, 5}, "text", 0.9F},
		{BoxKind::Text, {std::numeric_limits<int>::max(), 0, 5, 5}, "text", 0.9F},
		{BoxKind::Defect, {0, 0, 5, 5}, "defect",
			std::numeric_limits<float>::quiet_NaN()},
		{BoxKind::Object, {0, 0, 5, 5}, "product", 1.1F},
	};
	for (const auto& detection : invalid) {
		RoutingAlgorithm algorithm(detections({detection}));
		auto result = algorithm.process(packet);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.status().code(), core::StatusCode::InvalidArgument);
	}
}

TEST(RoutingAlgorithm, PropagatesDetectorFailureAndPreservesPerRegionOcrFailure) {
	cv::Mat image(20, 20, CV_8UC3, cv::Scalar(0));
	pipeline::PipelinePacket packet(makeFrame(image));
	RoutingAlgorithm failed([](const cv::Mat&) {
		return core::Result<std::vector<Detection>>::failure(
			core::Status::error(core::StatusCode::BackendError, "detector failed"));
	});
	auto failure = failed.process(packet);
	ASSERT_FALSE(failure);
	EXPECT_EQ(failure.status().code(), core::StatusCode::BackendError);
	RoutingAlgorithm ocrFailure(mockYoloDetect, [](const cv::Mat&) {
		return core::Result<OcrText>::failure(
			core::Status::error(core::StatusCode::BackendError, "OCR failed"));
	});
	auto result = ocrFailure.process(packet);
	ASSERT_TRUE(result);
	EXPECT_EQ(result.value().objects.size(), 1U);
	EXPECT_EQ(result.value().texts.at(0).recognition.status().code(),
		core::StatusCode::BackendError);
}

TEST(RoutingAlgorithm, RejectsMissingImageOrFunctionsAndUnsupportedFormat) {
	pipeline::PipelinePacket missing(vision::Frame{});
	missing.finishPostprocess();
	RoutingAlgorithm algorithm;
	auto noImage = algorithm.process(missing);
	ASSERT_FALSE(noImage);
	EXPECT_EQ(noImage.status().code(), core::StatusCode::InvalidState);
	cv::Mat image(20, 20, CV_8UC3, cv::Scalar(0));
	pipeline::PipelinePacket packet(makeFrame(image));
	RoutingAlgorithm noFunctions(Detector{}, OcrRecognizer{});
	EXPECT_FALSE(noFunctions.process(packet));
	cv::Mat gray16(20, 20, CV_16UC1, cv::Scalar(0));
	pipeline::PipelinePacket unsupported(makeFrame(gray16, vision::PixelFormat::Gray16));
	auto result = algorithm.process(unsupported);
	ASSERT_FALSE(result);
	EXPECT_EQ(result.status().code(), core::StatusCode::Unsupported);
}

TEST(RoutingAlgorithm, DeliversClassificationCallbackAndOwnedAnnotationsAsynchronously) {
	pipeline::OpenCvPipelineBuilder<InspectionResult> builder;
	auto built = builder.setAlgorithm(std::make_unique<RoutingAlgorithm>()).build();
	ASSERT_TRUE(built);
	executor::SerialPipelineExecutor<InspectionResult> executor(
		std::make_unique<pipeline::OpenCvPipeline<InspectionResult>>(
			std::move(built).value()));
	std::size_t classifications = 0;
	cv::Mat annotation;
	auto submitted = executor.submit(
		pipeline::PipelinePacket(makeFrame(
			cv::Mat(40, 80, CV_8UC3, cv::Scalar(0)))),
		[&](executor::TaskId, const core::Result<InspectionResult>& result) {
			ASSERT_TRUE(result);
			classifications += result.value().objects.size();
			annotation = result.value().annotatedImage;
		});
	ASSERT_TRUE(submitted);
	executor.requestStop();
	executor.wait();
	const auto& result = submitted.value().future().get();
	ASSERT_TRUE(result);
	EXPECT_EQ(classifications, 1U);
	EXPECT_EQ(annotation.size(), cv::Size(80, 40));
	EXPECT_EQ(result.value().defects.size(), 1U);
}

} // namespace
} // namespace visionSample::yoloRouting
