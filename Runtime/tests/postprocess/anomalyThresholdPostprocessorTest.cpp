#include "postprocess/anomalyThresholdPostprocessor.hpp"

#include "core/tensor.hpp"
#include "memory/cpuAllocator.hpp"

#include <gtest/gtest.h>
#include <limits>

namespace {

visionRuntime::preprocess::TensorMap scalarOutput(float score) {
	auto tensor = visionRuntime::memory::CpuAllocator{}.allocateTensor(
		visionRuntime::core::DataType::Float32,
		visionRuntime::core::TensorShape{1});
	if (!tensor) {
		return {};
	}
	*static_cast<float*>(tensor->data()) = score;
	visionRuntime::preprocess::TensorMap outputs;
	outputs.emplace("score", std::move(tensor).value());
	return outputs;
}

} // namespace

TEST(AnomalyThresholdPostprocessorTest, MarksScoresAtOrAboveThresholdAsAnomaly) {
	auto postprocessor = visionRuntime::postprocess::AnomalyThresholdPostprocessor::create({
		.outputName = "score",
		.threshold = 2.0F,
	});
	ASSERT_TRUE(postprocessor);

	auto result = postprocessor.value()->process(
		scalarOutput(2.0F), {}, visionRuntime::pipeline::PipelinePacket({}));

	ASSERT_TRUE(result);
	EXPECT_EQ(result->decision, visionRuntime::vision::AnomalyDecision::Ng);
	EXPECT_EQ(visionRuntime::vision::anomalyDecisionName(result->decision), "NG");
	EXPECT_FLOAT_EQ(result->score, 2.0F);
	EXPECT_FLOAT_EQ(result->threshold, 2.0F);
}

TEST(AnomalyThresholdPostprocessorTest, LeavesScoresBelowThresholdNormal) {
	auto postprocessor = visionRuntime::postprocess::AnomalyThresholdPostprocessor::create({
		.outputName = "score",
		.threshold = 2.0F,
	});
	ASSERT_TRUE(postprocessor);

	auto result = postprocessor.value()->process(
		scalarOutput(1.5F), {}, visionRuntime::pipeline::PipelinePacket({}));

	ASSERT_TRUE(result);
	EXPECT_EQ(result->decision, visionRuntime::vision::AnomalyDecision::Ok);
	EXPECT_EQ(visionRuntime::vision::anomalyDecisionName(result->decision), "OK");
	EXPECT_FLOAT_EQ(result->score, 1.5F);
	EXPECT_FLOAT_EQ(result->threshold, 2.0F);
}

TEST(AnomalyThresholdPostprocessorTest, RejectsNonFiniteScoresInsteadOfReturningOk) {
	auto postprocessor = visionRuntime::postprocess::AnomalyThresholdPostprocessor::create({
		.outputName = "score", .threshold = 2.0F});
	ASSERT_TRUE(postprocessor);
	for (const auto score : {std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(),
		-std::numeric_limits<float>::infinity()}) {
		auto result = postprocessor.value()->process(
			scalarOutput(score), {}, visionRuntime::pipeline::PipelinePacket({}));
		ASSERT_FALSE(result);
		EXPECT_EQ(result.status().code(), visionRuntime::core::StatusCode::DataLoss);
	}
}

TEST(AnomalyThresholdPostprocessorTest, RejectsMissingAndNonScalarOutputs) {
	auto postprocessor = visionRuntime::postprocess::AnomalyThresholdPostprocessor::create({
		.outputName = "score", .threshold = 2.0F});
	ASSERT_TRUE(postprocessor);
	auto missing = postprocessor.value()->process(
		{}, {}, visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_FALSE(missing);
	auto tensor = visionRuntime::memory::CpuAllocator{}.allocateTensor(
		visionRuntime::core::DataType::Float32, visionRuntime::core::TensorShape{2});
	ASSERT_TRUE(tensor);
	visionRuntime::preprocess::TensorMap outputs;
	outputs.emplace("score", std::move(tensor).value());
	auto nonscalar = postprocessor.value()->process(
		outputs, {}, visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_FALSE(nonscalar);
	EXPECT_EQ(nonscalar.status().code(), visionRuntime::core::StatusCode::Unsupported);
}