#include "core/tensorBuffer.hpp"
#include "preprocess/fusedImageToTensorNode.hpp"
#include "preprocess/preprocessChain.hpp"
#include "preprocess/tensorNormalizeNode.hpp"
#include "vision/frame.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <utility>

namespace {

using visonRuntime::preprocess::CameraFramePreprocessBuilder;
using visonRuntime::preprocess::FusedImageToTensorNode;
using visonRuntime::preprocess::PreprocessDataState;
using visonRuntime::preprocess::TensorNormalizeNode;

using CameraBuilder =
	visonRuntime::preprocess::PreprocessChainBuilder<PreprocessDataState::CameraFrame, false>;
using TensorBuilder =
	visonRuntime::preprocess::PreprocessChainBuilder<PreprocessDataState::Tensor, true>;

template<typename Builder, typename Node>
concept CanAppend = requires(Builder builder, Node node) {
	std::move(builder).then(std::move(node));
};

static_assert(CanAppend<CameraBuilder, FusedImageToTensorNode>);
static_assert(!CanAppend<TensorBuilder, FusedImageToTensorNode>);
static_assert(CanAppend<TensorBuilder, TensorNormalizeNode>);
static_assert(!CanAppend<CameraBuilder, TensorNormalizeNode>);

visonRuntime::vision::Frame makeFrame() {
	auto storage = std::make_shared<std::array<std::uint8_t, 12>>(
		std::array<std::uint8_t, 12>{
			10, 20, 30, 40, 50, 60,
			70, 80, 90, 100, 110, 120});
	auto buffer = visonRuntime::core::TensorBuffer::share(
		storage, storage->data(), storage->size());
	if (!buffer) {
		return {};
	}
	auto frame = visonRuntime::vision::Frame::create(
		std::move(buffer).value(), 2, 2,
		visonRuntime::vision::PixelFormat::Bgr8);
	return frame ? std::move(frame).value() : visonRuntime::vision::Frame{};
}

} // namespace

TEST(PreprocessChainTest, MaterializesThenNormalizesInPlace) {
	using namespace visonRuntime;

	preprocess::FusedImageToTensorOptions materializeOptions;
	materializeOptions.inputName = "image";
	materializeOptions.width = 2;
	materializeOptions.height = 2;
	materializeOptions.bufferCount = 1;
	auto materialize =
		preprocess::FusedImageToTensorNode::create(materializeOptions);
	ASSERT_TRUE(materialize);

	preprocess::TensorNormalizeOptions normalizeOptions;
	normalizeOptions.inputName = "image";
	normalizeOptions.scale = 1.0F;
	normalizeOptions.mean = {1.0F, 2.0F, 3.0F};
	normalizeOptions.standardDeviation = {2.0F, 4.0F, 5.0F};
	auto normalize = preprocess::TensorNormalizeNode::create(normalizeOptions);
	ASSERT_TRUE(normalize);

	auto chain = CameraFramePreprocessBuilder::start()
		.then(std::move(materialize).value())
		.then(std::move(normalize).value())
		.build();
	auto prepared = chain->process(pipeline::PipelinePacket(makeFrame()));
	ASSERT_TRUE(prepared);
	EXPECT_FALSE(prepared->packet().hasCameraFrame());
	const auto& tensor = prepared->tensors().at("image");
	const auto* values = static_cast<const float*>(tensor.data());
	ASSERT_NE(values, nullptr);
	EXPECT_FLOAT_EQ(values[0], 14.5F);
	EXPECT_FLOAT_EQ(values[4], 4.5F);
	EXPECT_FLOAT_EQ(values[8], 1.4F);
}