#include "camera/frameBufferPool.hpp"
#include "core/tensor.hpp"
#include "vision/frame.hpp"

#include <gtest/gtest.h>

TEST(FrameBufferPoolTest, ReturnsSlotAfterLastFrameViewIsReleased) {
	using namespace visonRuntime;

	auto poolResult = camera::FrameBufferPool::create(1, 64);
	ASSERT_TRUE(poolResult);
	auto pool = std::move(poolResult).value();

	{
		auto buffer = pool.acquire();
		ASSERT_TRUE(buffer);
		EXPECT_EQ(pool.available(), 0U);
		{
			auto frame = vision::Frame::create(
				std::move(buffer).value(), 4, 4, vision::PixelFormat::Bgr8);
			ASSERT_TRUE(frame);
			EXPECT_EQ(pool.available(), 0U);
			EXPECT_EQ(frame->rowStride(), 12U);
			EXPECT_EQ(frame->byteSize(), 48U);
		}
	}

	EXPECT_EQ(pool.available(), 1U);
	EXPECT_TRUE(pool.acquire());
}

TEST(FrameBufferPoolTest, TensorViewKeepsPoolLeaseAlive) {
	using namespace visonRuntime;

	auto poolResult = camera::FrameBufferPool::create(1, 32);
	ASSERT_TRUE(poolResult);
	auto pool = std::move(poolResult).value();
	{
		auto buffer = pool.acquire();
		ASSERT_TRUE(buffer);
		auto tensor = core::Tensor::wrap(
			std::move(buffer).value(), core::DataType::UInt8, core::TensorShape{32});
		ASSERT_TRUE(tensor);
		EXPECT_FALSE(pool.acquire());
	}
	EXPECT_EQ(pool.available(), 1U);
}

TEST(FrameBufferPoolTest, RejectsFrameLargerThanSlot) {
	using namespace visonRuntime;

	auto poolResult = camera::FrameBufferPool::create(1, 16);
	ASSERT_TRUE(poolResult);
	auto buffer = poolResult->acquire();
	ASSERT_TRUE(buffer);
	auto frame = vision::Frame::create(
		buffer.value(), 4, 4, vision::PixelFormat::Bgr8);

	EXPECT_FALSE(frame);
	EXPECT_EQ(frame.status().code(), core::StatusCode::InvalidArgument);
}