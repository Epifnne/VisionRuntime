#include "core/tensor.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <vector>

TEST(TensorTest, AllocatesContiguousCpuTensor) {
	using namespace visonRuntime::core;

	auto result = Tensor::allocate(
		DataType::Float32, TensorShape{1, 3, 4, 5}, TensorLayout::Nchw);

	ASSERT_TRUE(result);
	EXPECT_EQ(result->elementCount(), 60U);
	EXPECT_EQ(result->byteSize(), 240U);
	EXPECT_EQ(result->byteStrides(), (Tensor::ByteStrides{240, 80, 20, 4}));
	EXPECT_TRUE(result->isContiguous());
	EXPECT_EQ(result->bytes().size(), 240U);
	EXPECT_EQ(result->ownership(), TensorOwnership::Owned);
	EXPECT_TRUE(result->spec().shape.has_value());
}

TEST(TensorTest, WrapsPaddedSharedBuffer) {
	using namespace visonRuntime::core;

	auto storage = std::shared_ptr<void>(::operator new(32), [](void* pointer) {
		::operator delete(pointer);
	});
	auto result = Tensor::share(
		storage, 32, DataType::UInt8, TensorShape{2, 3}, Device::cpu(),
		TensorLayout::Any, {16, 1});

	ASSERT_TRUE(result);
	EXPECT_EQ(result->byteSize(), 19U);
	EXPECT_FALSE(result->isContiguous());
	EXPECT_EQ(result->ownership(), TensorOwnership::Shared);
	EXPECT_EQ(result->data(), storage.get());
}

TEST(TensorTest, RejectsDynamicShapeAndInsufficientBuffer) {
	using namespace visonRuntime::core;

	auto storage = std::make_shared<std::vector<std::byte>>(8);
	auto buffer = TensorBuffer::share(storage, storage->data(), storage->size());
	ASSERT_TRUE(buffer);
	auto dynamic = Tensor::wrap(
		buffer.value(), DataType::Float32, TensorShape{-1, 2});
	auto tooSmall = Tensor::wrap(
		buffer.value(), DataType::Float32, TensorShape{2, 2});

	EXPECT_FALSE(dynamic);
	EXPECT_EQ(dynamic.status().code(), StatusCode::InvalidArgument);
	EXPECT_FALSE(tooSmall);
	EXPECT_EQ(tooSmall.status().code(), StatusCode::InvalidArgument);
}

TEST(TensorTest, HidesHostByteSpanForDeviceMemory) {
	using namespace visonRuntime::core;

	auto placeholder = std::make_shared<std::vector<std::byte>>(4);
	auto buffer = TensorBuffer::share(
		placeholder, placeholder->data(), placeholder->size(),
		Device::cuda(), MemoryKind::Device);
	ASSERT_TRUE(buffer);
	auto result = Tensor::wrap(
		buffer.value(), DataType::Float32, TensorShape{1});

	ASSERT_TRUE(result);
	EXPECT_NE(result->data(), nullptr);
	EXPECT_TRUE(result->bytes().empty());
}

TEST(TensorTest, CreatesSubviewWithoutCopyingBuffer) {
	using namespace visonRuntime::core;

	auto parent = Tensor::allocate(DataType::UInt8, TensorShape{16});
	ASSERT_TRUE(parent);
	auto child = parent->subview(4, DataType::UInt8, TensorShape{8});

	ASSERT_TRUE(child);
	EXPECT_EQ(child->byteOffset(), 4U);
	EXPECT_EQ(child->data(), static_cast<std::byte*>(parent->data()) + 4);
	EXPECT_EQ(child->capacity(), parent->capacity());
	EXPECT_EQ(child->memoryKind(), MemoryKind::Host);
}

TEST(TensorTest, KeepsExternalOwnerAliveForViews) {
	using namespace visonRuntime::core;

	auto owner = std::make_shared<std::vector<std::byte>>(32);
	auto buffer = TensorBuffer::share(owner, owner->data(), owner->size());
	ASSERT_TRUE(buffer);
	auto tensor = Tensor::wrap(buffer.value(), DataType::UInt8, TensorShape{32});
	ASSERT_TRUE(tensor);

	owner.reset();
	EXPECT_EQ(tensor->bytes().size(), 32U);
	EXPECT_EQ(tensor->data(), buffer->data());
}

TEST(TensorTest, DoesNotExposeMutableAccessToReadOnlyBuffer) {
	using namespace visonRuntime::core;

	auto owner = std::make_shared<std::vector<std::byte>>(4);
	auto buffer = TensorBuffer::share(
		owner, owner->data(), owner->size(), Device::cpu(), MemoryKind::Host, false);
	ASSERT_TRUE(buffer);
	auto tensor = Tensor::wrap(buffer.value(), DataType::UInt8, TensorShape{4});
	ASSERT_TRUE(tensor);

	EXPECT_EQ(tensor->data(), nullptr);
	EXPECT_TRUE(tensor->bytes().empty());
	const auto& constTensor = tensor.value();
	EXPECT_EQ(constTensor.bytes().size(), 4U);
}