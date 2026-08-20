#include "core/tensorBuffer.hpp"
#include "preprocess/frameNodes/centerCropNode.hpp"
#include "preprocess/frameNodes/resizeNode.hpp"
#include "preprocess/frameNodes/toTensorNode.hpp"
#include "preprocess/preprocessChain.hpp"
#include "preprocess/tensorNodes/normalizeNode.hpp"
#include "vision/frame.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <utility>

namespace {

using visionRuntime::preprocess::Normalize;
using visionRuntime::preprocess::PreprocessBuilder;
using visionRuntime::preprocess::CenterCrop;
using visionRuntime::preprocess::Resize;
using visionRuntime::preprocess::ToTensor;

using CameraBuilder = decltype(PreprocessBuilder::start<visionRuntime::vision::Frame>());
using TensorBuilder = decltype(
	std::declval<CameraBuilder>()
		.then(CenterCrop({2, 2}))
		.then(ToTensor()));

struct CustomTensorNode {
	static constexpr auto inputState =
		visionRuntime::preprocess::PreprocessDataState::Tensor;
	static constexpr auto outputState =
		visionRuntime::preprocess::PreprocessDataState::Tensor;
	static constexpr bool materializes = false;

	visionRuntime::core::Result<std::unique_ptr<
		visionRuntime::preprocess::IPreprocessNode>> build(
		visionRuntime::preprocess::PreprocessBuildContext&) &&;
};

template<typename Builder, typename Node>
concept CanAppend = requires(Builder builder, Node node) {
	std::move(builder).then(std::move(node));
};

static_assert(CanAppend<CameraBuilder, ToTensor>);
static_assert(!CanAppend<TensorBuilder, ToTensor>);
static_assert(CanAppend<TensorBuilder, Normalize>);
static_assert(!CanAppend<CameraBuilder, Normalize>);
static_assert(visionRuntime::preprocess::PreprocessNode<CustomTensorNode>);
static_assert(CanAppend<TensorBuilder, CustomTensorNode>);

visionRuntime::vision::Frame makeFrame() {
	auto storage = std::make_shared<std::array<std::uint8_t, 12>>(
		std::array<std::uint8_t, 12>{
			10, 20, 30, 40, 50, 60,
			70, 80, 90, 100, 110, 120});
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

visionRuntime::vision::Frame makeWideGrayFrame() {
	auto storage = std::make_shared<std::array<std::uint8_t, 8>>(
		std::array<std::uint8_t, 8>{10, 20, 30, 40, 50, 60, 70, 80});
	auto buffer = visionRuntime::core::TensorBuffer::share(
		storage, storage->data(), storage->size());
	if (!buffer) {
		return {};
	}
	auto frame = visionRuntime::vision::Frame::create(
		std::move(buffer).value(), 4, 2,
		visionRuntime::vision::PixelFormat::Gray8);
	return frame ? std::move(frame).value() : visionRuntime::vision::Frame{};
}

} // namespace

TEST(PreprocessChainTest, MaterializesThenNormalizesInPlace) {
	using namespace visionRuntime;

	preprocess::ToTensorOptions toTensorOptions;
	toTensorOptions.tensorName = "image";
	toTensorOptions.bufferCount = 1;

	preprocess::NormalizeOptions normalizeOptions;
	normalizeOptions.scale = 1.0F;
	normalizeOptions.mean = {1.0F, 2.0F, 3.0F};
	normalizeOptions.standardDeviation = {2.0F, 4.0F, 5.0F};

	auto chain = PreprocessBuilder::start<vision::Frame>()
		.then(preprocess::CenterCrop({2, 2}))
		.then(preprocess::ToTensor(std::move(toTensorOptions)))
		.then(preprocess::Normalize(std::move(normalizeOptions)))
		.build();
	ASSERT_TRUE(chain);
	auto prepared = chain.value()->process(pipeline::PipelinePacket(makeFrame()));
	ASSERT_TRUE(prepared);
	EXPECT_FALSE(prepared->packet().hasCameraFrame());
	const auto& tensor = prepared->tensors().at("image");
	const auto* values = static_cast<const float*>(tensor.data());
	ASSERT_NE(values, nullptr);
	EXPECT_FLOAT_EQ(values[0], 14.5F);
	EXPECT_FLOAT_EQ(values[4], 4.5F);
	EXPECT_FLOAT_EQ(values[8], 1.4F);
}

TEST(PreprocessChainTest, MaterializesAndNormalizesSingleChannelFrame) {
	using namespace visionRuntime;

	preprocess::ToTensorOptions toTensorOptions;
	toTensorOptions.tensorName = "image";
	toTensorOptions.bufferCount = 1;
	toTensorOptions.channels = 1;
	preprocess::NormalizeOptions normalizeOptions;
	normalizeOptions.scale = 1.0F;
	normalizeOptions.mean = {10.0F};
	normalizeOptions.standardDeviation = {2.0F};

	auto chain = PreprocessBuilder::start<vision::Frame>()
		.then(preprocess::CenterCrop({4, 2}))
		.then(preprocess::ToTensor(std::move(toTensorOptions)))
		.then(preprocess::Normalize(std::move(normalizeOptions)))
		.build();
	ASSERT_TRUE(chain);
	auto prepared = chain.value()->process(
		pipeline::PipelinePacket(makeWideGrayFrame()));
	ASSERT_TRUE(prepared);
	const auto& tensor = prepared->tensors().at("image");
	EXPECT_EQ(tensor.shape(), core::TensorShape({1, 1, 2, 4}));
	const auto* values = static_cast<const float*>(tensor.data());
	ASSERT_NE(values, nullptr);
	EXPECT_FLOAT_EQ(values[0], 0.0F);
	EXPECT_FLOAT_EQ(values[7], 35.0F);
}

TEST(PreprocessChainTest, ResizesShortSideAndCenterCrops) {
	using namespace visionRuntime;

	preprocess::ToTensorOptions options;
	options.tensorName = "image";
	options.bufferCount = 1;
	auto chain = PreprocessBuilder::start<vision::Frame>()
		.then(preprocess::Resize::shortSide(2))
		.then(preprocess::CenterCrop({2, 2}))
		.then(preprocess::ToTensor(std::move(options)))
		.build();
	ASSERT_TRUE(chain);
	auto prepared = chain.value()->process(pipeline::PipelinePacket(makeWideGrayFrame()));
	ASSERT_TRUE(prepared);
	const auto& tensor = prepared->tensors().at("image");
	const auto* values = static_cast<const float*>(tensor.data());
	ASSERT_NE(values, nullptr);
	EXPECT_FLOAT_EQ(values[0], 20.0F);
	EXPECT_FLOAT_EQ(values[1], 30.0F);
	EXPECT_FLOAT_EQ(values[2], 60.0F);
	EXPECT_FLOAT_EQ(values[3], 70.0F);
	EXPECT_FLOAT_EQ(prepared->transformContext().scaleX, 1.0F);
	EXPECT_FLOAT_EQ(prepared->transformContext().scaleY, 1.0F);
	EXPECT_EQ(prepared->transformContext().crop.left, 1U);
	EXPECT_EQ(prepared->transformContext().crop.top, 0U);
}

TEST(PreprocessChainTest, ReportsInvalidNodeConfigurationAtBuild) {
	using namespace visionRuntime;

	auto chain = preprocess::PreprocessBuilder::start<vision::Frame>()
		.then(preprocess::Resize::shortSide(0))
		.then(preprocess::CenterCrop({224, 224}))
		.then(preprocess::ToTensor())
		.then(preprocess::Normalize())
		.build();

	EXPECT_FALSE(chain);
	EXPECT_EQ(chain.status().code(), core::StatusCode::InvalidArgument);
}