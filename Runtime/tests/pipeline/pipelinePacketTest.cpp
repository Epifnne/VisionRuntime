#include "camera/frameBufferPool.hpp"
#include "pipeline/businessFramePool.hpp"
#include "pipeline/pipelinePacket.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace {

static_assert(!std::is_copy_constructible_v<visionRuntime::pipeline::PipelinePacket>);
static_assert(!std::is_copy_assignable_v<visionRuntime::pipeline::PipelinePacket>);
static_assert(std::is_nothrow_move_constructible_v<visionRuntime::pipeline::PipelinePacket>);
static_assert(std::is_nothrow_move_assignable_v<visionRuntime::pipeline::PipelinePacket>);
static_assert(std::is_same_v<decltype(
	std::declval<const visionRuntime::pipeline::PipelinePacket&>().payload<int>()),
	const int*>);
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

struct InspectionPayload {
	std::string workpieceId;
	int threshold;
};

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

TEST(PipelinePacketTest, CarriesOptionalSourceIdThroughMoves) {
	using namespace visionRuntime;

	auto cameraPoolResult = camera::FrameBufferPool::create(1, 48);
	ASSERT_TRUE(cameraPoolResult);
	auto cameraPool = std::move(cameraPoolResult).value();

	pipeline::PipelinePacket untagged(acquireFrame(cameraPool));
	EXPECT_FALSE(untagged.sourceId().has_value());

	pipeline::PipelinePacket tagged(
		acquireFrame(cameraPool), {}, std::uint32_t{7});
	ASSERT_TRUE(tagged.sourceId().has_value());
	EXPECT_EQ(*tagged.sourceId(), 7U);

	pipeline::PipelinePacket moved(std::move(tagged));
	ASSERT_TRUE(moved.sourceId().has_value());
	EXPECT_EQ(*moved.sourceId(), 7U);

	moved.setSourceId(std::nullopt);
	EXPECT_FALSE(moved.sourceId().has_value());
}

TEST(PipelinePacketTest, RetrievesAndReplacesTypedPayload) {
	using namespace visionRuntime;
	pipeline::PipelinePacket packet({});
	const auto& constPacket = packet;
	EXPECT_EQ(constPacket.payload<InspectionPayload>(), nullptr);

	InspectionPayload payload{"part-001", 7};
	packet.setPayload(payload);
	payload.workpieceId = "changed";
	const auto* stored = constPacket.payload<InspectionPayload>();
	ASSERT_NE(stored, nullptr);
	EXPECT_EQ(stored->workpieceId, "part-001");
	EXPECT_EQ(stored->threshold, 7);
	EXPECT_EQ(constPacket.payload<std::string>(), nullptr);

	packet.setPayload(std::string("replacement"));
	EXPECT_EQ(constPacket.payload<InspectionPayload>(), nullptr);
	ASSERT_NE(constPacket.payload<std::string>(), nullptr);
	EXPECT_EQ(*constPacket.payload<std::string>(), "replacement");
}

TEST(PipelinePacketTest, MovesPayloadWithPacketConstructionAndAssignment) {
	using namespace visionRuntime;
	pipeline::PipelinePacket source({}, {}, std::uint32_t{7});
	source.setPayload(InspectionPayload{"part-002", 9});
	const auto executionId = source.executionId();

	pipeline::PipelinePacket constructed(std::move(source));
	EXPECT_EQ(source.payload<InspectionPayload>(), nullptr);
	ASSERT_NE(constructed.payload<InspectionPayload>(), nullptr);
	EXPECT_EQ(constructed.payload<InspectionPayload>()->workpieceId, "part-002");
	EXPECT_EQ(constructed.executionId(), executionId);

	pipeline::PipelinePacket assigned({});
	assigned.setPayload(std::string("previous"));
	assigned = std::move(constructed);
	EXPECT_EQ(constructed.payload<InspectionPayload>(), nullptr);
	EXPECT_EQ(assigned.payload<std::string>(), nullptr);
	ASSERT_NE(assigned.payload<InspectionPayload>(), nullptr);
	EXPECT_EQ(assigned.payload<InspectionPayload>()->threshold, 9);
	EXPECT_EQ(assigned.executionId(), executionId);
	EXPECT_EQ(assigned.sourceId(), std::uint32_t{7});
}

TEST(PipelinePacketTest, KeepsPayloadAfterImagePreparationAndResetsAtFinish) {
	using namespace visionRuntime;
	auto cameraPoolResult = camera::FrameBufferPool::create(1, 48);
	auto businessPoolResult = camera::FrameBufferPool::create(1, 48);
	ASSERT_TRUE(cameraPoolResult);
	ASSERT_TRUE(businessPoolResult);
	auto cameraPool = std::move(cameraPoolResult).value();
	auto businessPool = std::move(businessPoolResult).value();
	std::weak_ptr<std::string> weakPayload;
	{
		pipeline::PipelinePacket packet(acquireFrame(cameraPool));
		auto value = std::make_shared<std::string>("part-003");
		weakPayload = value;
		packet.setPayload(value);
		value.reset();

		packet.completeImagePreparation(acquireFrame(businessPool));
		EXPECT_EQ(cameraPool.available(), 1U);
		ASSERT_NE(packet.payload<std::shared_ptr<std::string>>(), nullptr);
		EXPECT_EQ(**packet.payload<std::shared_ptr<std::string>>(), "part-003");

		packet.finishPostprocess();
		EXPECT_EQ(businessPool.available(), 1U);
		EXPECT_EQ(packet.payload<std::shared_ptr<std::string>>(), nullptr);
		EXPECT_TRUE(weakPayload.expired());
		packet.finishPostprocess();
		EXPECT_EQ(packet.payload<std::shared_ptr<std::string>>(), nullptr);
	}
	EXPECT_TRUE(weakPayload.expired());
}

TEST(PipelinePacketTest, ReleasesPayloadOnDestructionWithoutFinish) {
	using namespace visionRuntime;
	std::weak_ptr<int> weakPayload;
	{
		pipeline::PipelinePacket packet({});
		auto value = std::make_shared<int>(42);
		weakPayload = value;
		packet.setPayload(std::move(value));
		EXPECT_FALSE(weakPayload.expired());
	}
	EXPECT_TRUE(weakPayload.expired());
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