#include "camera/frameBufferPool.hpp"
#include "core/tensor.hpp"
#include "vision/frame.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <stop_token>

TEST(FrameBufferPoolTest, ReturnsSlotAfterLastFrameViewIsReleased) {
	using namespace visionRuntime;

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
	using namespace visionRuntime;

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
	using namespace visionRuntime;

	auto poolResult = camera::FrameBufferPool::create(1, 16);
	ASSERT_TRUE(poolResult);
	auto buffer = poolResult->acquire();
	ASSERT_TRUE(buffer);
	auto frame = vision::Frame::create(
		buffer.value(), 4, 4, vision::PixelFormat::Bgr8);

	EXPECT_FALSE(frame);
	EXPECT_EQ(frame.status().code(), core::StatusCode::InvalidArgument);
}

TEST(FrameBufferPoolTest, DropPolicyDoesNotOverwriteLeasedSlot) {
	using namespace visionRuntime;

	auto poolResult = camera::FrameBufferPool::create(
		{1, 32, camera::BufferFullPolicy::Drop});
	ASSERT_TRUE(poolResult);
	auto pool = std::move(poolResult).value();
	auto leased = pool.acquire();
	ASSERT_TRUE(leased);

	auto dropped = pool.acquire();

	ASSERT_FALSE(dropped);
	EXPECT_EQ(dropped.status().code(), core::StatusCode::ResourceExhausted);
}

TEST(FrameBufferPoolTest, BlockPolicyWaitsUntilLeaseIsReleased) {
	using namespace std::chrono_literals;
	using namespace visionRuntime;

	auto poolResult = camera::FrameBufferPool::create(
		{1, 32, camera::BufferFullPolicy::Block});
	ASSERT_TRUE(poolResult);
	auto pool = std::move(poolResult).value();
	auto acquired = pool.acquire();
	ASSERT_TRUE(acquired);
	auto leased = std::move(acquired).value();
	auto waiting = std::async(std::launch::async, [&pool] {
		return pool.acquire();
	});
	EXPECT_EQ(waiting.wait_for(20ms), std::future_status::timeout);

	leased = {};

	EXPECT_EQ(waiting.wait_for(1s), std::future_status::ready);
	EXPECT_TRUE(waiting.get());
}

TEST(FrameBufferPoolTest, BlockPolicyCanBeCancelledWhileWaiting) {
	using namespace std::chrono_literals;
	using namespace visionRuntime;

	auto poolResult = camera::FrameBufferPool::create(
		{1, 32, camera::BufferFullPolicy::Block});
	ASSERT_TRUE(poolResult);
	auto pool = std::move(poolResult).value();
	auto leased = pool.acquire();
	ASSERT_TRUE(leased);
	std::stop_source stopSource;
	auto waiting = std::async(std::launch::async, [&pool, &stopSource] {
		return pool.acquire(stopSource.get_token());
	});
	EXPECT_EQ(waiting.wait_for(20ms), std::future_status::timeout);

	stopSource.request_stop();

	ASSERT_EQ(waiting.wait_for(1s), std::future_status::ready);
	auto cancelled = waiting.get();
	ASSERT_FALSE(cancelled);
	EXPECT_EQ(cancelled.status().code(), core::StatusCode::Cancelled);
}