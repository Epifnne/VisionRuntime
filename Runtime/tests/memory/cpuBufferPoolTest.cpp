 #include "memory/cpuAllocator.hpp"
#include "memory/cpuBufferPool.hpp"

#include <gtest/gtest.h>

#include <memory>

TEST(CpuAllocatorTest, AllocatesWritableHostBuffer) {
	using namespace visionRuntime;

	memory::CpuAllocator allocator;
	auto buffer = allocator.allocate(64);

	ASSERT_TRUE(buffer);
	EXPECT_EQ(buffer->capacity(), 64U);
	EXPECT_EQ(buffer->device(), core::Device::cpu());
	EXPECT_EQ(buffer->memoryKind(), core::MemoryKind::Host);
	EXPECT_TRUE(buffer->isHostAccessible());
	EXPECT_TRUE(buffer->isWritable());
}

TEST(CpuBufferPoolTest, ReusesBuffersFromSharedAllocator) {
	using namespace visionRuntime;

	auto allocator = std::make_shared<memory::CpuAllocator>();
	auto pool = memory::CpuBufferPool::create(1, 32, allocator);
	ASSERT_TRUE(pool);

	void* firstAddress = nullptr;
	{
		auto first = pool->acquire();
		ASSERT_TRUE(first);
		firstAddress = first->data();
		EXPECT_EQ(pool->available(), 0U);
	}

	EXPECT_EQ(pool->available(), 1U);
	auto second = pool->acquire();
	ASSERT_TRUE(second);
	EXPECT_EQ(second->data(), firstAddress);
}