#include "core/result.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

TEST(ResultTest, HoldsValue) {
	using visonRuntime::core::Result;

	auto text = Result<std::string>::success("inference complete");
	EXPECT_TRUE(text.hasValue());
	EXPECT_TRUE(text);
	EXPECT_TRUE(text.status().isOk());
	EXPECT_EQ(text.value(), "inference complete");
	EXPECT_EQ(text->size(), 18);
	EXPECT_EQ(*text, "inference complete");
}

TEST(ResultTest, MovesOwnedValueOut) {
	using visonRuntime::core::Result;

	auto pointer = Result<std::unique_ptr<int>>::success(std::make_unique<int>(42));
	std::unique_ptr<int> movedPointer = std::move(pointer).value();
	ASSERT_NE(movedPointer, nullptr);
	EXPECT_EQ(*movedPointer, 42);
}

TEST(ResultTest, DistinguishesStatusValueFromErrorStatus) {
	using visonRuntime::core::Result;
	using visonRuntime::core::Status;

	auto statusValue = Result<Status>::success(Status::ok());
	EXPECT_TRUE(statusValue.hasValue());
	EXPECT_TRUE(statusValue.value().isOk());
}

TEST(ResultTest, HoldsError) {
	using visonRuntime::core::Result;
	using visonRuntime::core::Status;
	using visonRuntime::core::StatusCode;

	auto error = Result<int>::failure(Status::error(
		StatusCode::QueueFull,
		"executor queue reached its capacity"));
	EXPECT_FALSE(error.hasValue());
	EXPECT_FALSE(error);
	EXPECT_EQ(error.status().code(), StatusCode::QueueFull);
	EXPECT_EQ(error.status().message(), "executor queue reached its capacity");
}

TEST(ResultVoidTest, RepresentsSuccessfulOperation) {
	using visonRuntime::core::Result;

	auto operation = Result<void>::success();
	EXPECT_TRUE(operation.hasValue());
	operation.value();
}

TEST(ResultVoidTest, RepresentsFailedOperation) {
	using visonRuntime::core::Result;
	using visonRuntime::core::Status;
	using visonRuntime::core::StatusCode;

	auto failedOperation = Result<void>::failure(
		Status::error(StatusCode::InvalidState, "frame source is already stopped"));
	EXPECT_FALSE(failedOperation);
	EXPECT_EQ(failedOperation.status().code(), StatusCode::InvalidState);
}