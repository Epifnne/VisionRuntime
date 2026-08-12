#include "pipeline/openCvPipelineBuilder.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {

class RecordingAlgorithm final
	: public visonRuntime::pipeline::IOpenCvAlgorithm<int> {
public:
	explicit RecordingAlgorithm(bool& called) : called_(called) {}

	visonRuntime::core::Result<int> process(
		const visonRuntime::pipeline::PipelinePacket&) override {
		called_ = true;
		return visonRuntime::core::Result<int>::success(23);
	}

private:
	bool& called_;
};

class FailingAlgorithm final
	: public visonRuntime::pipeline::IOpenCvAlgorithm<int> {
public:
	visonRuntime::core::Result<int> process(
		const visonRuntime::pipeline::PipelinePacket&) override {
		return visonRuntime::core::Result<int>::failure(
			visonRuntime::core::Status::error(
				visonRuntime::core::StatusCode::InvalidArgument,
				"unsupported image format"));
	}
};

class ThrowingAlgorithm final
	: public visonRuntime::pipeline::IOpenCvAlgorithm<int> {
public:
	visonRuntime::core::Result<int> process(
		const visonRuntime::pipeline::PipelinePacket&) override {
		throw std::runtime_error("OpenCV operation failed");
	}
};

} // namespace

TEST(OpenCvPipelineBuilderTest, RejectsMissingAlgorithm) {
	visonRuntime::pipeline::OpenCvPipelineBuilder<int> builder;
	auto result = builder.build();

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visonRuntime::core::StatusCode::InvalidState);
}

TEST(OpenCvPipelineTest, RunsThroughCommonPipelineInterface) {
	bool called = false;
	visonRuntime::pipeline::OpenCvPipelineBuilder<int> builder;
	auto pipelineResult = builder
		.setAlgorithm(std::make_unique<RecordingAlgorithm>(called))
		.build();
	ASSERT_TRUE(pipelineResult);

	std::unique_ptr<visonRuntime::pipeline::IVisionPipeline<int>> pipeline =
		std::make_unique<visonRuntime::pipeline::OpenCvPipeline<int>>(
			std::move(pipelineResult).value());
	auto result = pipeline->run(visonRuntime::pipeline::PipelinePacket({}));

	ASSERT_TRUE(result);
	EXPECT_EQ(result.value(), 23);
	EXPECT_TRUE(called);
}

TEST(OpenCvPipelineTest, AddsStageContextToAlgorithmFailure) {
	visonRuntime::pipeline::OpenCvPipelineBuilder<int> builder;
	auto pipelineResult = builder
		.setAlgorithm(std::make_unique<FailingAlgorithm>())
		.build();
	ASSERT_TRUE(pipelineResult);

	auto result = pipelineResult->run(visonRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visonRuntime::core::StatusCode::InvalidArgument);
	EXPECT_NE(result.status().toString().find("opencv"), std::string::npos);
}

TEST(OpenCvPipelineTest, ConvertsAlgorithmExceptionsToInternalStatus) {
	visonRuntime::pipeline::OpenCvPipelineBuilder<int> builder;
	auto pipelineResult = builder
		.setAlgorithm(std::make_unique<ThrowingAlgorithm>())
		.build();
	ASSERT_TRUE(pipelineResult);

	auto result = pipelineResult->run(visonRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visonRuntime::core::StatusCode::Internal);
	EXPECT_NE(result.status().message().find("OpenCV operation failed"), std::string::npos);
}