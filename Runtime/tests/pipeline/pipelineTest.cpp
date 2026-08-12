#include "pipeline/pipelineBuilder.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using TensorMap = visonRuntime::preprocess::TensorMap;

class RecordingPreprocessor final : public visonRuntime::preprocess::IPreprocessor {
public:
	explicit RecordingPreprocessor(std::vector<std::string>& calls) : calls_(calls) {}

	visonRuntime::core::Result<visonRuntime::preprocess::PreparedInput> process(
		visonRuntime::pipeline::PipelinePacket packet) override {
		calls_.push_back("preprocess");
		return visonRuntime::core::Result<visonRuntime::preprocess::PreparedInput>::success(
			visonRuntime::preprocess::PreparedInput(std::move(packet), {}));
	}

private:
	std::vector<std::string>& calls_;
};

class RecordingBackend final : public visonRuntime::backends::IInferenceBackend {
public:
	explicit RecordingBackend(std::vector<std::string>& calls) : calls_(calls) {}

	visonRuntime::core::Result<TensorMap> infer(const TensorMap&) override {
		calls_.push_back("inference");
		return visonRuntime::core::Result<TensorMap>::success({});
	}

private:
	std::vector<std::string>& calls_;
};

class RecordingPostprocessor final
	: public visonRuntime::postprocess::IPostprocessor<int> {
public:
	explicit RecordingPostprocessor(std::vector<std::string>& calls) : calls_(calls) {}

	visonRuntime::core::Result<int> process(
		const TensorMap&,
		const visonRuntime::vision::TransformContext&,
		const visonRuntime::pipeline::PipelinePacket&) override {
		calls_.push_back("postprocess");
		return visonRuntime::core::Result<int>::success(42);
	}

private:
	std::vector<std::string>& calls_;
};

class FailingBackend final : public visonRuntime::backends::IInferenceBackend {
public:
	visonRuntime::core::Result<TensorMap> infer(const TensorMap&) override {
		return visonRuntime::core::Result<TensorMap>::failure(
			visonRuntime::core::Status::error(
				visonRuntime::core::StatusCode::BackendError, "model execution failed"));
	}
};

class ThrowingPostprocessor final
	: public visonRuntime::postprocess::IPostprocessor<int> {
public:
	visonRuntime::core::Result<int> process(
		const TensorMap&,
		const visonRuntime::vision::TransformContext&,
		const visonRuntime::pipeline::PipelinePacket&) override {
		throw std::runtime_error("decoder failure");
	}
};

visonRuntime::pipeline::PipelineBuilder<int> makeBuilder(
	std::vector<std::string>& calls) {
	visonRuntime::pipeline::PipelineBuilder<int> builder;
	builder
		.setPreprocessor(std::make_unique<RecordingPreprocessor>(calls))
		.setBackend(std::make_unique<RecordingBackend>(calls))
		.setPostprocessor(std::make_unique<RecordingPostprocessor>(calls));
	return builder;
}

} // namespace

TEST(PipelineBuilderTest, RejectsMissingStages) {
	visonRuntime::pipeline::PipelineBuilder<int> builder;
	auto result = builder.build();

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visonRuntime::core::StatusCode::InvalidState);
}

TEST(PipelineTest, RunsLinearStagesInOrder) {
	std::vector<std::string> calls;
	auto pipelineResult = makeBuilder(calls).build();
	ASSERT_TRUE(pipelineResult);
	std::unique_ptr<visonRuntime::pipeline::IVisionPipeline<int>> pipeline =
		std::make_unique<visonRuntime::pipeline::ModelPipeline<int>>(
			std::move(pipelineResult).value());

	auto result = pipeline->run(visonRuntime::pipeline::PipelinePacket({}));

	ASSERT_TRUE(result);
	EXPECT_EQ(result.value(), 42);
	EXPECT_EQ(calls, (std::vector<std::string>{"preprocess", "inference", "postprocess"}));
}

TEST(PipelineTest, StopsAfterBackendFailureAndAddsStageContext) {
	std::vector<std::string> calls;
	visonRuntime::pipeline::PipelineBuilder<int> builder;
	builder
		.setPreprocessor(std::make_unique<RecordingPreprocessor>(calls))
		.setBackend(std::make_unique<FailingBackend>())
		.setPostprocessor(std::make_unique<RecordingPostprocessor>(calls));
	auto pipelineResult = builder.build();
	ASSERT_TRUE(pipelineResult);

	auto result = pipelineResult->run(visonRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visonRuntime::core::StatusCode::BackendError);
	EXPECT_EQ(calls, (std::vector<std::string>{"preprocess"}));
	EXPECT_NE(result.status().toString().find("inference"), std::string::npos);
}

TEST(PipelineTest, ConvertsStageExceptionsToInternalStatus) {
	std::vector<std::string> calls;
	visonRuntime::pipeline::PipelineBuilder<int> builder;
	builder
		.setPreprocessor(std::make_unique<RecordingPreprocessor>(calls))
		.setBackend(std::make_unique<RecordingBackend>(calls))
		.setPostprocessor(std::make_unique<ThrowingPostprocessor>());
	auto pipelineResult = builder.build();
	ASSERT_TRUE(pipelineResult);

	auto result = pipelineResult->run(visonRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visonRuntime::core::StatusCode::Internal);
	EXPECT_NE(result.status().message().find("decoder failure"), std::string::npos);
}