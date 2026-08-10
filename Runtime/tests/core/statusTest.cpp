#include "core/status.hpp"

#include <gtest/gtest.h>

TEST(StatusTest, RepresentsSuccess) {
	using visonRuntime::core::Status;
	using visonRuntime::core::StatusCode;

	const Status ok = Status::ok();
	EXPECT_TRUE(ok.isOk());
	EXPECT_TRUE(ok);
	EXPECT_EQ(ok.code(), StatusCode::Ok);
	EXPECT_TRUE(ok.message().empty());
	EXPECT_EQ(ok.toString(), "Ok");
}

TEST(StatusTest, RepresentsError) {
	using visonRuntime::core::Status;
	using visonRuntime::core::StatusCode;

	const Status backendError = Status::error(
		StatusCode::BackendError,
		"OpenVINO failed to compile the model");
	EXPECT_FALSE(backendError.isOk());
	EXPECT_FALSE(backendError);
	EXPECT_EQ(backendError.code(), StatusCode::BackendError);
	EXPECT_EQ(backendError.message(), "OpenVINO failed to compile the model");
}

TEST(StatusTest, PrependsContextWithoutChangingMessage) {
	using visonRuntime::core::Status;
	using visonRuntime::core::StatusCode;

	const Status backendError = Status::error(
		StatusCode::BackendError,
		"OpenVINO failed to compile the model");

	const Status contextualError = backendError
		.withContext("loading model anomaly.onnx")
		.withContext("creating runtime");
	EXPECT_EQ(contextualError.message(), backendError.message());
	EXPECT_EQ(
		contextualError.toString(),
		"BackendError: creating runtime: loading model anomaly.onnx: OpenVINO failed to compile the model");
}