#include "core/tensorBuffer.hpp"
#include "pipeline/pipelinePacket.hpp"
#include "preprocess/openCvNchwPreprocessor.hpp"
#include "vision/frame.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace {

visionRuntime::vision::Frame makeBgrFrame(
	std::array<std::uint8_t, 12> pixels) {
	auto storage = std::make_shared<std::array<std::uint8_t, 12>>(pixels);
	auto buffer = visionRuntime::core::TensorBuffer::share(
		storage, storage->data(), storage->size());
	if (!buffer) {
		return {};
	}
	auto frame = visionRuntime::vision::Frame::create(
		std::move(buffer).value(), 2, 2,
		visionRuntime::vision::PixelFormat::Bgr8);
	return frame ? std::move(frame).value() : visionRuntime::vision::Frame{};
}

} // namespace

TEST(OpenCvNchwPreprocessorTest, WritesRgbNchwDirectlyIntoPooledTensor) {
	using namespace visionRuntime;

	preprocess::OpenCvNchwPreprocessorOptions options;
	options.inputName = "image";
	options.width = 2;
	options.height = 2;
	options.bufferCount = 1;
	options.scale = 1.0F;
	options.mean = {1.0F, 2.0F, 3.0F};
	options.standardDeviation = {2.0F, 4.0F, 5.0F};
	auto preprocessorResult = preprocess::OpenCvNchwPreprocessor::create(options);
	ASSERT_TRUE(preprocessorResult);
	auto preprocessor = std::move(preprocessorResult).value();

	{
		pipeline::PipelinePacket packet(makeBgrFrame({
			10, 20, 30, 40, 50, 60,
			70, 80, 90, 100, 110, 120}));
		auto prepared = preprocessor->process(std::move(packet));
		ASSERT_TRUE(prepared);
		ASSERT_EQ(prepared->tensors().size(), 1U);
		const auto& tensor = prepared->tensors().at("image");
		EXPECT_EQ(tensor.dataType(), core::DataType::Float32);
		EXPECT_EQ(tensor.layout(), core::TensorLayout::Nchw);
		EXPECT_EQ(tensor.shape(), core::TensorShape({1, 3, 2, 2}));

		const auto* values = static_cast<const float*>(tensor.data());
		ASSERT_NE(values, nullptr);
		EXPECT_FLOAT_EQ(values[0], 14.5F);
		EXPECT_FLOAT_EQ(values[1], 29.5F);
		EXPECT_FLOAT_EQ(values[2], 44.5F);
		EXPECT_FLOAT_EQ(values[3], 59.5F);
		EXPECT_FLOAT_EQ(values[4], 4.5F);
		EXPECT_FLOAT_EQ(values[5], 12.0F);
		EXPECT_FLOAT_EQ(values[6], 19.5F);
		EXPECT_FLOAT_EQ(values[7], 27.0F);
		EXPECT_FLOAT_EQ(values[8], 1.4F);
		EXPECT_FLOAT_EQ(values[9], 7.4F);
		EXPECT_FLOAT_EQ(values[10], 13.4F);
		EXPECT_FLOAT_EQ(values[11], 19.4F);
		EXPECT_EQ(preprocessor->availableBuffers(), 0U);
		EXPECT_FALSE(prepared->packet().hasCameraFrame());
		EXPECT_EQ(prepared->transformContext().sourceSize.width, 2U);
		EXPECT_EQ(prepared->transformContext().networkSize.width, 2U);
	}

	EXPECT_EQ(preprocessor->availableBuffers(), 1U);
}

TEST(OpenCvNchwPreprocessorTest, RejectsInvalidConfiguration) {
	visionRuntime::preprocess::OpenCvNchwPreprocessorOptions options;
	options.width = 2;
	options.height = 2;
	options.standardDeviation[1] = 0.0F;

	auto preprocessor =
		visionRuntime::preprocess::OpenCvNchwPreprocessor::create(options);

	EXPECT_FALSE(preprocessor);
	EXPECT_EQ(preprocessor.status().code(),
		visionRuntime::core::StatusCode::InvalidArgument);
}