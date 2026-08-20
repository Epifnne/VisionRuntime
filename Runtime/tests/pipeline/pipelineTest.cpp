#include "benchmark/anomalyCsvTimedPipeline.hpp"
#include "pipeline/pipelineBuilder.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using TensorMap = visionRuntime::preprocess::TensorMap;

class RecordingPreprocessor final : public visionRuntime::preprocess::IPreprocessor {
public:
	explicit RecordingPreprocessor(std::vector<std::string>& calls) : calls_(calls) {}

	visionRuntime::core::Result<visionRuntime::preprocess::PreparedInput> process(
		visionRuntime::pipeline::PipelinePacket packet) override {
		calls_.push_back("preprocess");
		return visionRuntime::core::Result<visionRuntime::preprocess::PreparedInput>::success(
			visionRuntime::preprocess::PreparedInput(std::move(packet), {}));
	}

private:
	std::vector<std::string>& calls_;
};

class RecordingBackend final : public visionRuntime::backends::IInferenceBackend {
public:
	explicit RecordingBackend(std::vector<std::string>& calls) : calls_(calls) {}

	visionRuntime::core::Result<TensorMap> infer(const TensorMap&) override {
		calls_.push_back("inference");
		return visionRuntime::core::Result<TensorMap>::success({});
	}

private:
	std::vector<std::string>& calls_;
};

class RecordingPostprocessor final
	: public visionRuntime::postprocess::IPostprocessor<int> {
public:
	explicit RecordingPostprocessor(std::vector<std::string>& calls) : calls_(calls) {}

	visionRuntime::core::Result<int> process(
		const TensorMap&,
		const visionRuntime::vision::TransformContext&,
		const visionRuntime::pipeline::PipelinePacket&) override {
		calls_.push_back("postprocess");
		return visionRuntime::core::Result<int>::success(42);
	}

private:
	std::vector<std::string>& calls_;
};

class FailingBackend final : public visionRuntime::backends::IInferenceBackend {
public:
	visionRuntime::core::Result<TensorMap> infer(const TensorMap&) override {
		return visionRuntime::core::Result<TensorMap>::failure(
			visionRuntime::core::Status::error(
				visionRuntime::core::StatusCode::BackendError, "model execution failed"));
	}
};

class ThrowingPostprocessor final
	: public visionRuntime::postprocess::IPostprocessor<int> {
public:
	visionRuntime::core::Result<int> process(
		const TensorMap&,
		const visionRuntime::vision::TransformContext&,
		const visionRuntime::pipeline::PipelinePacket&) override {
		throw std::runtime_error("decoder failure");
	}
};

class AnomalyPostprocessor final
	: public visionRuntime::postprocess::IPostprocessor<
		visionRuntime::vision::AnomalyResult> {
public:
	visionRuntime::core::Result<visionRuntime::vision::AnomalyResult> process(
		const TensorMap&,
		const visionRuntime::vision::TransformContext&,
		const visionRuntime::pipeline::PipelinePacket&) override {
		return visionRuntime::core::Result<
			visionRuntime::vision::AnomalyResult>::success({
				.score = 2.5F,
				.threshold = 2.0F,
				.decision = visionRuntime::vision::AnomalyDecision::Ng});
	}
};

visionRuntime::pipeline::PipelineBuilder<int> makeBuilder(
	std::vector<std::string>& calls) {
	visionRuntime::pipeline::PipelineBuilder<int> builder;
	builder
		.setPreprocessor(std::make_unique<RecordingPreprocessor>(calls))
		.setBackend(std::make_unique<RecordingBackend>(calls))
		.setPostprocessor(std::make_unique<RecordingPostprocessor>(calls));
	return builder;
}

visionRuntime::pipeline::PipelineBuilder<visionRuntime::vision::AnomalyResult>
makeAnomalyBuilder(std::vector<std::string>& calls) {
	visionRuntime::pipeline::PipelineBuilder<visionRuntime::vision::AnomalyResult> builder;
	builder
		.setPreprocessor(std::make_unique<RecordingPreprocessor>(calls))
		.setBackend(std::make_unique<RecordingBackend>(calls))
		.setPostprocessor(std::make_unique<AnomalyPostprocessor>());
	return builder;
}

} // namespace

TEST(PipelineBuilderTest, RejectsMissingStages) {
	visionRuntime::pipeline::PipelineBuilder<int> builder;
	auto result = builder.build();

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visionRuntime::core::StatusCode::InvalidState);
}

TEST(PipelineTest, RunsLinearStagesInOrder) {
	std::vector<std::string> calls;
	auto pipelineResult = makeBuilder(calls).build();
	ASSERT_TRUE(pipelineResult);
	std::unique_ptr<visionRuntime::pipeline::IVisionPipeline<int>> pipeline =
		std::make_unique<visionRuntime::pipeline::ModelPipeline<int>>(
			std::move(pipelineResult).value());

	auto result = pipeline->run(visionRuntime::pipeline::PipelinePacket({}));

	ASSERT_TRUE(result);
	EXPECT_EQ(result.value(), 42);
	EXPECT_EQ(calls, (std::vector<std::string>{"preprocess", "inference", "postprocess"}));
}

TEST(PipelineTest, StopsAfterBackendFailureAndAddsStageContext) {
	std::vector<std::string> calls;
	visionRuntime::pipeline::PipelineBuilder<int> builder;
	builder
		.setPreprocessor(std::make_unique<RecordingPreprocessor>(calls))
		.setBackend(std::make_unique<FailingBackend>())
		.setPostprocessor(std::make_unique<RecordingPostprocessor>(calls));
	auto pipelineResult = builder.build();
	ASSERT_TRUE(pipelineResult);

	auto result = pipelineResult->run(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visionRuntime::core::StatusCode::BackendError);
	EXPECT_EQ(calls, (std::vector<std::string>{"preprocess"}));
	EXPECT_NE(result.status().toString().find("inference"), std::string::npos);
}

TEST(PipelineTest, ConvertsStageExceptionsToInternalStatus) {
	std::vector<std::string> calls;
	visionRuntime::pipeline::PipelineBuilder<int> builder;
	builder
		.setPreprocessor(std::make_unique<RecordingPreprocessor>(calls))
		.setBackend(std::make_unique<RecordingBackend>(calls))
		.setPostprocessor(std::make_unique<ThrowingPostprocessor>());
	auto pipelineResult = builder.build();
	ASSERT_TRUE(pipelineResult);

	auto result = pipelineResult->run(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visionRuntime::core::StatusCode::Internal);
	EXPECT_NE(result.status().message().find("decoder failure"), std::string::npos);
}

TEST(TimedPipelineTest, ReportsSuccessfulStageDurations) {
	std::vector<std::string> calls;
	auto pipelineResult = makeBuilder(calls).build();
	ASSERT_TRUE(pipelineResult);
	bool observed = false;
	visionRuntime::benchmark::TimedPipeline<int> pipeline(
		std::move(pipelineResult).value(),
		[&](std::uint64_t sequenceNumber,
			const visionRuntime::core::Result<int>& result,
			const visionRuntime::benchmark::PipelineDurations& durations) {
			observed = true;
			EXPECT_EQ(sequenceNumber, 0);
			ASSERT_TRUE(result);
			EXPECT_EQ(result.value(), 42);
			EXPECT_GE(durations.preprocessMilliseconds, 0.0);
			EXPECT_GE(durations.inferenceMilliseconds, 0.0);
			EXPECT_GE(durations.postprocessMilliseconds, 0.0);
			EXPECT_GE(durations.stageMilliseconds, 0.0);
			EXPECT_GE(durations.waitMilliseconds, 0.0);
			EXPECT_GE(durations.latencyMilliseconds,
				durations.stageMilliseconds);
		});

	auto result = pipeline.run(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_TRUE(result);
	EXPECT_TRUE(observed);
}

TEST(TimedPipelineTest, ReportsBackendFailure) {
	std::vector<std::string> calls;
	visionRuntime::pipeline::PipelineBuilder<int> builder;
	builder
		.setPreprocessor(std::make_unique<RecordingPreprocessor>(calls))
		.setBackend(std::make_unique<FailingBackend>())
		.setPostprocessor(std::make_unique<RecordingPostprocessor>(calls));
	auto pipelineResult = builder.build();
	ASSERT_TRUE(pipelineResult);
	bool observed = false;
	visionRuntime::benchmark::TimedPipeline<int> pipeline(
		std::move(pipelineResult).value(),
		[&](std::uint64_t,
			const visionRuntime::core::Result<int>& result,
			const visionRuntime::benchmark::PipelineDurations&) {
			observed = true;
			EXPECT_FALSE(result);
			EXPECT_EQ(result.status().code(),
				visionRuntime::core::StatusCode::BackendError);
		});

	auto result = pipeline.run(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(result);
	EXPECT_TRUE(observed);
}

TEST(AnomalyCsvTimedPipelineTest, DoesNotOpenOutputWhenDeactivated) {
	std::vector<std::string> calls;
	auto pipelineResult = makeAnomalyBuilder(calls).build();
	ASSERT_TRUE(pipelineResult);
	visionRuntime::benchmark::AnomalyCsvTimingOptions options;
	options.outputPath = visionRuntime::benchmark::TimingOutputPath::file(
		std::filesystem::path{});

	auto timedPipeline = visionRuntime::benchmark::makeAnomalyCsvTimedPipeline(
		std::move(pipelineResult).value(), options);

	ASSERT_TRUE(timedPipeline);
	EXPECT_TRUE(timedPipeline.value()->run(
		visionRuntime::pipeline::PipelinePacket({})));
}

TEST(AnomalyCsvTimedPipelineTest, WritesHeaderAndResultToFile) {
	const auto outputPath = std::filesystem::temp_directory_path() /
		"visionRuntime-anomaly-timing.csv";
	std::filesystem::remove(outputPath);
	std::vector<std::string> calls;
	auto pipelineResult = makeAnomalyBuilder(calls).build();
	ASSERT_TRUE(pipelineResult);
	visionRuntime::benchmark::AnomalyCsvTimingOptions options;
	options.activate = true;
	options.outputPath =
		visionRuntime::benchmark::TimingOutputPath::file(outputPath);
	{
		auto timedPipeline = visionRuntime::benchmark::makeAnomalyCsvTimedPipeline(
			std::move(pipelineResult).value(), options);
		ASSERT_TRUE(timedPipeline);
		EXPECT_TRUE(timedPipeline.value()->run(
			visionRuntime::pipeline::PipelinePacket({})));
	}

	{
		std::ifstream output(outputPath);
		const std::string contents{
			std::istreambuf_iterator<char>(output), std::istreambuf_iterator<char>()};
		EXPECT_NE(contents.find("sequence,score,threshold,result,pre_ms,"),
			std::string::npos);
		EXPECT_NE(contents.find("stage_ms,wait_ms,latency_ms"),
			std::string::npos);
		EXPECT_NE(contents.find("0,2.5,2,NG,"), std::string::npos);
	}
	std::filesystem::remove(outputPath);
}