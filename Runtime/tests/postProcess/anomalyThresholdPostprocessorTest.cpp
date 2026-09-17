#include "postProcess/anomalyThresholdPostprocessor.hpp"

#include "core/tensor.hpp"
#include "memory/cpuAllocator.hpp"

#include <gtest/gtest.h>

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