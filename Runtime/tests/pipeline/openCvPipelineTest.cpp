#include "pipeline/openCvPipelineBuilder.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {

class RecordingAlgorithm final
	: public visionRuntime::pipeline::IOpenCvAlgorithm<int> {
public:
	explicit RecordingAlgorithm(bool& called) : called_(called) {}

	visionRuntime::core::Result<int> process(
		const visionRuntime::pipeline::PipelinePacket&) override {
		called_ = true;
		return visionRuntime::core::Result<int>::success(23);
	}

private:
	bool& called_;
};

class FailingAlgorithm final
	: public visionRuntime::pipeline::IOpenCvAlgorithm<int> {
public:
	visionRuntime::core::Result<int> process(
		const visionRuntime::pipeline::PipelinePacket&) override {
		return visionRuntime::core::Result<int>::failure(
			visionRuntime::core::Status::error(
				visionRuntime::core::StatusCode::InvalidArgument,
				"unsupported image format"));
	}
};

class ThrowingAlgorithm final
	: public visionRuntime::pipeline::IOpenCvAlgorithm<int> {
public:
	visionRuntime::core::Result<int> process(
		const visionRuntime::pipeline::PipelinePacket&) override {
		throw std::runtime_error("OpenCV operation failed");
	}
};

} // namespace

TEST(OpenCvPipelineBuilderTest, RejectsMissingAlgorithm) {
	visionRuntime::pipeline::OpenCvPipelineBuilder<int> builder;
	auto result = builder.build();

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visionRuntime::core::StatusCode::InvalidState);
}

TEST(OpenCvPipelineTest, RunsThroughCommonPipelineInterface) {
	bool called = false;
	visionRuntime::pipeline::OpenCvPipelineBuilder<int> builder;
	auto pipelineResult = builder
		.setAlgorithm(std::make_unique<RecordingAlgorithm>(called))
		.build();
	ASSERT_TRUE(pipelineResult);

	std::unique_ptr<visionRuntime::pipeline::IVisionPipeline<int>> pipeline =
		std::make_unique<visionRuntime::pipeline::OpenCvPipeline<int>>(
			std::move(pipelineResult).value());
	auto result = pipeline->run(visionRuntime::pipeline::PipelinePacket({}));

	ASSERT_TRUE(result);
	EXPECT_EQ(result.value(), 23);
	EXPECT_TRUE(called);
}

TEST(OpenCvPipelineTest, AddsStageContextToAlgorithmFailure) {
	visionRuntime::pipeline::OpenCvPipelineBuilder<int> builder;
	auto pipelineResult = builder
		.setAlgorithm(std::make_unique<FailingAlgorithm>())
		.build();
	ASSERT_TRUE(pipelineResult);

	auto result = pipelineResult->run(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visionRuntime::core::StatusCode::InvalidArgument);
	EXPECT_NE(result.status().toString().find("opencv"), std::string::npos);
}

TEST(OpenCvPipelineTest, ConvertsAlgorithmExceptionsToInternalStatus) {
	visionRuntime::pipeline::OpenCvPipelineBuilder<int> builder;
	auto pipelineResult = builder
		.setAlgorithm(std::make_unique<ThrowingAlgorithm>())
		.build();
	ASSERT_TRUE(pipelineResult);

	auto result = pipelineResult->run(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visionRuntime::core::StatusCode::Internal);
	EXPECT_NE(result.status().message().find("OpenCV operation failed"), std::string::npos);
}