#include "cudaThresholdAlgorithm.hpp"

#include "pipeline/openCvPipelineBuilder.hpp"

#include <opencv2/imgproc.hpp>
#include <gtest/gtest.h>

#include <future>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace visionSample::openCvCuda {
namespace {
using namespace visionRuntime;

vision::Frame makeFrame(const cv::Mat& image,
	vision::PixelFormat format = vision::PixelFormat::Gray8,
	std::uint64_t sequence = 42) {
	auto owner = std::make_shared<cv::Mat>(image);
	const auto capacity = (static_cast<std::size_t>(image.rows) - 1) * image.step[0] +
		static_cast<std::size_t>(image.cols) * image.elemSize();
	auto buffer = core::TensorBuffer::share(owner, owner->data, capacity,
		core::Device::cpu(), core::MemoryKind::Host, false);
	if (!buffer) {
		throw std::runtime_error(buffer.status().toString());
	}
	auto frame = vision::Frame::create(std::move(buffer).value(),
		static_cast<std::size_t>(image.cols), static_cast<std::size_t>(image.rows),
		format, image.step[0], {.sequenceNumber = sequence});
	if (!frame) {
		throw std::runtime_error(frame.status().toString());
	}
	return std::move(frame).value();
}

TEST(CudaThresholdAlgorithm, RejectsInvalidConfiguration) {
	for (const auto threshold : {-1.0, 256.0, std::numeric_limits<double>::quiet_NaN()}) {
		const auto result = CudaThresholdAlgorithm::create(0, threshold);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.status().code(), core::StatusCode::InvalidArgument);
	}
	for (const auto fraction : {-0.1, 1.1, std::numeric_limits<double>::infinity()}) {
		const auto result = CudaThresholdAlgorithm::create(0, 127, fraction);
		ASSERT_FALSE(result);
		EXPECT_EQ(result.status().code(), core::StatusCode::InvalidArgument);
	}
	const auto negative = CudaThresholdAlgorithm::create(-1);
	ASSERT_FALSE(negative);
	EXPECT_EQ(negative.status().code(), core::StatusCode::InvalidArgument);
}

TEST(CudaThresholdAlgorithm, RejectsUnavailableDeviceWithoutFallback) {
	const auto result = CudaThresholdAlgorithm::create(std::numeric_limits<int>::max());
	ASSERT_FALSE(result);
	EXPECT_FALSE(result.status().isOk());
}

TEST(CudaThresholdAlgorithm, MatchesCpuForReadOnlyStridedInputAndChangingSizes) {
	auto created = CudaThresholdAlgorithm::create();
	ASSERT_TRUE(created) << created.status().toString();
	auto algorithm = std::move(created).value();
	for (const auto size : {cv::Size(1, 1), cv::Size(17, 9), cv::Size(640, 480)}) {
		cv::Mat storage(size.height + 2, size.width + 6, CV_8UC1);
		cv::RNG random(12345);
		random.fill(storage, cv::RNG::UNIFORM, 0, 256);
		const auto image = storage(cv::Rect(2, 1, size.width, size.height));
		const auto original = image.clone();
		cv::Mat mask;
		cv::threshold(image, mask, 127, 255, cv::THRESH_BINARY);
		const auto expected = static_cast<double>(cv::countNonZero(mask)) /
			static_cast<double>(image.total());
		auto result = algorithm->process(pipeline::PipelinePacket(makeFrame(image)));
		ASSERT_TRUE(result) << result.status().toString();
		EXPECT_DOUBLE_EQ(result->brightFraction, expected);
		EXPECT_EQ(result->accepted, expected >= 0.5);
		EXPECT_EQ(result->sequence, 42U);
		EXPECT_EQ(cv::norm(image, original, cv::NORM_INF), 0);
	}
}

TEST(CudaThresholdAlgorithm, UsesStrictPixelThresholdAndInclusiveAcceptanceThreshold) {
	auto created = CudaThresholdAlgorithm::create();
	ASSERT_TRUE(created) << created.status().toString();
	cv::Mat image(2, 4, CV_8UC1, cv::Scalar(127));
	image.colRange(2, 4).setTo(128);
	pipeline::OpenCvPipelineBuilder<InspectionResult> builder;
	auto pipeline = builder.setAlgorithm(std::move(created).value()).build();
	ASSERT_TRUE(pipeline);
	const auto result = pipeline->run(pipeline::PipelinePacket(makeFrame(image)));
	ASSERT_TRUE(result) << result.status().toString();
	EXPECT_DOUBLE_EQ(result->brightFraction, 0.5);
	EXPECT_TRUE(result->accepted);
}

TEST(CudaThresholdAlgorithm, RejectsMissingAndUnsupportedFrames) {
	auto created = CudaThresholdAlgorithm::create();
	ASSERT_TRUE(created) << created.status().toString();
	auto algorithm = std::move(created).value();
	auto missing = algorithm->process(pipeline::PipelinePacket({}));
	ASSERT_FALSE(missing);
	EXPECT_EQ(missing.status().code(), core::StatusCode::InvalidArgument);
	cv::Mat color(4, 4, CV_8UC3, cv::Scalar(255, 255, 255));
	auto unsupported = algorithm->process(pipeline::PipelinePacket(
		makeFrame(color, vision::PixelFormat::Bgr8)));
	ASSERT_FALSE(unsupported);
	EXPECT_EQ(unsupported.status().code(), core::StatusCode::Unsupported);
	auto owner = std::make_shared<int>(0);
	auto buffer = core::TensorBuffer::share(owner, owner.get(), sizeof(int),
		core::Device::cpu(), core::MemoryKind::Device);
	ASSERT_TRUE(buffer);
	auto frame = vision::Frame::create(std::move(buffer).value(), 1, 1, vision::PixelFormat::Gray8);
	ASSERT_TRUE(frame);
	const auto deviceOnly = algorithm->process(
		pipeline::PipelinePacket(std::move(frame).value()));
	ASSERT_FALSE(deviceOnly);
	EXPECT_EQ(deviceOnly.status().code(), core::StatusCode::InvalidArgument);
}

TEST(CudaThresholdAlgorithm, HandlesThresholdAndFractionEndpoints) {
	for (const auto threshold : {0.0, 255.0}) {
		for (const auto minimum : {0.0, 1.0}) {
			auto created = CudaThresholdAlgorithm::create(0, threshold, minimum);
			ASSERT_TRUE(created) << created.status().toString();
			cv::Mat image(3, 5, CV_8UC1, cv::Scalar(255));
			const auto result = created.value()->process(
				pipeline::PipelinePacket(makeFrame(image)));
			ASSERT_TRUE(result) << result.status().toString();
			const auto expected = threshold == 255 ? 0.0 : 1.0;
			EXPECT_DOUBLE_EQ(result->brightFraction, expected);
			EXPECT_EQ(result->accepted, expected >= minimum);
		}
	}
}

TEST(CudaThresholdAlgorithm, SelectsDeviceOnWorkerThreadsAndSerializesSharedBuffers) {
	auto created = CudaThresholdAlgorithm::create();
	ASSERT_TRUE(created) << created.status().toString();
	auto algorithm = std::move(created).value();
	std::vector<std::future<core::Result<InspectionResult>>> tasks;
	for (int index = 0; index < 8; ++index) {
		tasks.push_back(std::async(std::launch::async, [&, index] {
			cv::Mat image(23, 31, CV_8UC1, cv::Scalar(index % 2 == 0 ? 0 : 255));
			return algorithm->process(pipeline::PipelinePacket(makeFrame(image,
				vision::PixelFormat::Gray8, static_cast<std::uint64_t>(index))));
		}));
	}
	for (std::size_t index = 0; index < tasks.size(); ++index) {
		const auto result = tasks[index].get();
		ASSERT_TRUE(result) << result.status().toString();
		EXPECT_EQ(result->sequence, index);
		EXPECT_DOUBLE_EQ(result->brightFraction, index % 2 == 0 ? 0.0 : 1.0);
		EXPECT_EQ(result->accepted, index % 2 != 0);
	}
}

} // namespace
} // namespace visionSample::openCvCuda
