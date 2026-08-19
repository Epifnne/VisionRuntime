#include "camera/frameBufferPool.hpp"
#include "pipeline/businessFramePool.hpp"
#include "pipeline/pipelinePacket.hpp"

#include <gtest/gtest.h>

#include <type_traits>
#include <utility>

namespace {

static_assert(!std::is_copy_constructible_v<visionRuntime::pipeline::PipelinePacket>);
static_assert(!std::is_copy_assignable_v<visionRuntime::pipeline::PipelinePacket>);
static_assert(std::is_nothrow_move_constructible_v<visionRuntime::pipeline::PipelinePacket>);
static_assert(!std::is_copy_constructible_v<visionRuntime::vision::Frame>);
static_assert(std::is_nothrow_move_constructible_v<visionRuntime::vision::Frame>);

visionRuntime::vision::Frame acquireFrame(
	const visionRuntime::camera::FrameBufferPool& pool) {
	auto buffer = pool.acquire();
	if (!buffer) {
		return {};
	}
	auto frame = visionRuntime::vision::Frame::create(
		std::move(buffer).value(), 4, 4, visionRuntime::vision::PixelFormat::Bgr8);
	return frame ? std::move(frame).value() : visionRuntime::vision::Frame{};
}

} // namespace

TEST(PipelinePacketTest, ReleasesCameraFrameAndKeepsBusinessFrameByDefault) {
	using namespace visionRuntime;

	auto cameraPoolResult = camera::FrameBufferPool::create(1, 48);
	auto businessPoolResult = camera::FrameBufferPool::create(1, 48);
	ASSERT_TRUE(cameraPoolResult);
	ASSERT_TRUE(businessPoolResult);
	auto cameraPool = std::move(cameraPoolResult).value();
	auto businessPool = std::move(businessPoolResult).value();
	pipeline::PipelinePacket packet(acquireFrame(cameraPool));
	ASSERT_TRUE(packet.hasCameraFrame());

	packet.completeImagePreparation(acquireFrame(businessPool));
	EXPECT_FALSE(packet.hasCameraFrame());
	EXPECT_TRUE(packet.hasBusinessFrame());
	EXPECT_EQ(cameraPool.available(), 1U);
	EXPECT_EQ(businessPool.available(), 0U);

	packet.finishPostprocess();
	EXPECT_FALSE(packet.hasBusinessFrame());
	EXPECT_EQ(businessPool.available(), 1U);
}

TEST(PipelinePacketTest, SupportsAlternativeReleaseStages) {
	using namespace visionRuntime;

	auto cameraPoolResult = camera::FrameBufferPool::create(1, 48);
	auto businessPoolResult = camera::FrameBufferPool::create(1, 48);
	ASSERT_TRUE(cameraPoolResult);
	ASSERT_TRUE(businessPoolResult);
	auto cameraPool = std::move(cameraPoolResult).value();
	auto businessPool = std::move(businessPoolResult).value();
	pipeline::PipelinePacket packet(
		acquireFrame(cameraPool),
		{pipeline::FrameReleaseStage::AfterPostprocess,
		 pipeline::FrameReleaseStage::AfterImagePreparation});

	packet.completeImagePreparation(acquireFrame(businessPool));
	EXPECT_TRUE(packet.hasCameraFrame());
	EXPECT_FALSE(packet.hasBusinessFrame());
	EXPECT_EQ(cameraPool.available(), 0U);
	EXPECT_EQ(businessPool.available(), 1U);

	packet.finishPostprocess();
	EXPECT_EQ(cameraPool.available(), 1U);
}

TEST(PipelinePacketTest, ZeroCopyBusinessViewKeepsCameraSlotUntilPostprocess) {
	using namespace visionRuntime;

	auto poolResult = camera::FrameBufferPool::create(1, 48);
	ASSERT_TRUE(poolResult);
	auto pool = std::move(poolResult).value();
	pipeline::PipelinePacket packet(acquireFrame(pool));
	ASSERT_NE(packet.cameraFrame(), nullptr);
	auto businessFrame = vision::Frame::create(
		packet.cameraFrame()->buffer(), 4, 4, vision::PixelFormat::Bgr8);
	ASSERT_TRUE(businessFrame);

	packet.completeImagePreparation(std::move(businessFrame).value());
	EXPECT_FALSE(packet.hasCameraFrame());
	EXPECT_TRUE(packet.hasBusinessFrame());
	EXPECT_EQ(pool.available(), 0U);

	packet.finishPostprocess();
	EXPECT_EQ(pool.available(), 1U);
}

TEST(PipelinePacketTest, TransfersPacketBetweenStagesByMove) {
	using namespace visionRuntime;

	auto cameraPoolResult = camera::FrameBufferPool::create(1, 48);
	auto businessPoolResult = camera::FrameBufferPool::create(1, 48);
	ASSERT_TRUE(cameraPoolResult);
	ASSERT_TRUE(businessPoolResult);
	auto cameraPool = std::move(cameraPoolResult).value();
	auto businessPool = std::move(businessPoolResult).value();
	pipeline::PipelinePacket preprocessPacket(acquireFrame(cameraPool));
	preprocessPacket.completeImagePreparation(acquireFrame(businessPool));

	pipeline::PipelinePacket inferPacket(std::move(preprocessPacket));
	EXPECT_FALSE(preprocessPacket.hasCameraFrame());
	EXPECT_FALSE(preprocessPacket.hasBusinessFrame());
	pipeline::PipelinePacket postprocessPacket(std::move(inferPacket));
	EXPECT_FALSE(inferPacket.hasCameraFrame());
	EXPECT_FALSE(inferPacket.hasBusinessFrame());
	EXPECT_FALSE(postprocessPacket.hasCameraFrame());
	EXPECT_TRUE(postprocessPacket.hasBusinessFrame());
	EXPECT_EQ(postprocessPacket.businessFrame()->width(), 4U);

	{
		auto renderedFrame = postprocessPacket.takeBusinessFrame();
		ASSERT_TRUE(renderedFrame);
		EXPECT_EQ(businessPool.available(), 0U);
	}
	EXPECT_EQ(businessPool.available(), 1U);
}

TEST(BusinessFramePoolTest, AcquiresFramesWithFixedPaddedLayout) {
	using namespace visionRuntime;

	auto poolResult = pipeline::BusinessFramePool::create(
		2, 4, 3, vision::PixelFormat::Bgr8, 16);
	ASSERT_TRUE(poolResult);
	auto pool = std::move(poolResult).value();
	auto frame = pool.acquire();

	ASSERT_TRUE(frame);
	EXPECT_EQ(frame->width(), 4U);
	EXPECT_EQ(frame->height(), 3U);
	EXPECT_EQ(frame->rowStride(), 16U);
	EXPECT_EQ(frame->byteSize(), 44U);
	EXPECT_EQ(pool.size(), 2U);
	EXPECT_EQ(pool.available(), 1U);
}

TEST(BusinessFramePoolTest, MovesSameBufferThroughPipelineUntilPostprocess) {
	using namespace visionRuntime;

	auto cameraPoolResult = camera::FrameBufferPool::create(1, 48);
	auto businessPoolResult = pipeline::BusinessFramePool::create(
		1, 4, 4, vision::PixelFormat::Bgr8);
	ASSERT_TRUE(cameraPoolResult);
	ASSERT_TRUE(businessPoolResult);
	auto cameraPool = std::move(cameraPoolResult).value();
	auto businessPool = std::move(businessPoolResult).value();
	pipeline::PipelinePacket preprocessPacket(acquireFrame(cameraPool));
	auto businessFrame = businessPool.acquire();
	ASSERT_TRUE(businessFrame);
	auto* originalData = businessFrame->data();

	preprocessPacket.completeImagePreparation(std::move(businessFrame).value());
	EXPECT_EQ(cameraPool.available(), 1U);
	EXPECT_EQ(businessPool.available(), 0U);
	pipeline::PipelinePacket inferPacket(std::move(preprocessPacket));
	pipeline::PipelinePacket postprocessPacket(std::move(inferPacket));
	ASSERT_NE(postprocessPacket.businessFrame(), nullptr);
	EXPECT_EQ(postprocessPacket.businessFrame()->data(), originalData);

	postprocessPacket.finishPostprocess();
	EXPECT_EQ(businessPool.available(), 1U);
}

TEST(BusinessFramePoolTest, ReportsExhaustionUntilFrameIsReleased) {
	using namespace visionRuntime;

	auto poolResult = pipeline::BusinessFramePool::create(
		1, 4, 4, vision::PixelFormat::Gray8);
	ASSERT_TRUE(poolResult);
	auto pool = std::move(poolResult).value();
	{
		auto frame = pool.acquire();
		ASSERT_TRUE(frame);
		auto exhausted = pool.acquire();
		EXPECT_FALSE(exhausted);
		EXPECT_EQ(exhausted.status().code(), core::StatusCode::ResourceExhausted);
	}
	EXPECT_TRUE(pool.acquire());
}

TEST(BusinessFramePoolTest, RejectsInvalidPixelFormat) {
	using namespace visionRuntime;

	auto pool = pipeline::BusinessFramePool::create(
		1, 4, 4, static_cast<vision::PixelFormat>(255));
	EXPECT_FALSE(pool);
	EXPECT_EQ(pool.status().code(), core::StatusCode::InvalidArgument);
}