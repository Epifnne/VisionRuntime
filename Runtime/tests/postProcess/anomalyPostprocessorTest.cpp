#include "postProcess/anomalyPostprocessor.hpp"
#include "postProcess/anomalyThresholdPostprocessor.hpp"

#include "core/tensor.hpp"

#include <gtest/gtest.h>

namespace {

visionRuntime::preprocess::TensorMap scalarOutput(float score) {
	auto tensor = visionRuntime::core::Tensor::allocate(
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

TEST(AnomalyPostprocessorTest, DecodesPatchCoreScalarScore) {
	visionRuntime::postprocess::AnomalyPostprocessorOptions options;
	options.outputName = "score";
	auto postprocessor = visionRuntime::postprocess::AnomalyPostprocessor::create(options);
	ASSERT_TRUE(postprocessor);

	auto result = postprocessor.value()->process(
		scalarOutput(2.90172958F), {}, visionRuntime::pipeline::PipelinePacket({}));

	ASSERT_TRUE(result);
	EXPECT_FLOAT_EQ(result->score, 2.90172958F);
	EXPECT_EQ(result->decision, visionRuntime::vision::AnomalyDecision::Unknown);
}

TEST(AnomalyThresholdPostprocessorTest, MarksScoresAtOrAboveThresholdAsAnomaly) {
	visionRuntime::postprocess::AnomalyPostprocessorOptions options;
	options.outputName = "score";
	auto scorePostprocessor = visionRuntime::postprocess::AnomalyPostprocessor::create(options);
	ASSERT_TRUE(scorePostprocessor);
	visionRuntime::postprocess::AnomalyThresholdPostprocessorOptions thresholdOptions;
	thresholdOptions.threshold = 2.0F;
	auto postprocessor = visionRuntime::postprocess::AnomalyThresholdPostprocessor::create(
		std::move(scorePostprocessor).value(), thresholdOptions);
	ASSERT_TRUE(postprocessor);

	auto result = postprocessor.value()->process(
		scalarOutput(2.0F), {}, visionRuntime::pipeline::PipelinePacket({}));

	ASSERT_TRUE(result);
	EXPECT_EQ(result->decision, visionRuntime::vision::AnomalyDecision::Ng);
	EXPECT_EQ(visionRuntime::vision::anomalyDecisionName(result->decision), "NG");
	EXPECT_FLOAT_EQ(result->threshold, 2.0F);
}

TEST(AnomalyThresholdPostprocessorTest, LeavesScoresBelowThresholdNormal) {
	visionRuntime::postprocess::AnomalyPostprocessorOptions options;
	options.outputName = "score";
	auto scorePostprocessor = visionRuntime::postprocess::AnomalyPostprocessor::create(options);
	ASSERT_TRUE(scorePostprocessor);
	visionRuntime::postprocess::AnomalyThresholdPostprocessorOptions thresholdOptions;
	thresholdOptions.threshold = 2.0F;
	auto postprocessor = visionRuntime::postprocess::AnomalyThresholdPostprocessor::create(
		std::move(scorePostprocessor).value(), thresholdOptions);
	ASSERT_TRUE(postprocessor);

	auto result = postprocessor.value()->process(
		scalarOutput(1.5F), {}, visionRuntime::pipeline::PipelinePacket({}));

	ASSERT_TRUE(result);
	EXPECT_EQ(result->decision, visionRuntime::vision::AnomalyDecision::Ok);
	EXPECT_EQ(visionRuntime::vision::anomalyDecisionName(result->decision), "OK");
	EXPECT_FLOAT_EQ(result->threshold, 2.0F);
}