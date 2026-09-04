#include "postProcess/anomalyPostprocessor.hpp"
#include "postProcess/anomalyThresholdPostprocessor.hpp"

#include "core/tensor.hpp"
#include "memory/cpuAllocator.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

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

visionRuntime::preprocess::TensorMap embeddingOutput() {
	auto tensor = visionRuntime::memory::CpuAllocator{}.allocateTensor(
		visionRuntime::core::DataType::Float32,
		visionRuntime::core::TensorShape{2, 2});
	if (!tensor) {
		return {};
	}
	auto* values = static_cast<float*>(tensor->data());
	values[0] = 1.0F;
	values[1] = 0.0F;
	values[2] = 4.0F;
	values[3] = 0.0F;
	visionRuntime::preprocess::TensorMap outputs;
	outputs.emplace("embedding", std::move(tensor).value());
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

TEST(AnomalyPostprocessorTest, ScoresPatchEmbeddingsWithFaissMemoryBank) {
	const auto bankPath = std::filesystem::temp_directory_path() /
		"vision-runtime-anomaly-memory-bank.f32";
	{
		const float bank[] = {0.0F, 0.0F, 2.0F, 0.0F};
		std::ofstream stream(bankPath, std::ios::binary);
		stream.write(reinterpret_cast<const char*>(bank), sizeof(bank));
	}

	visionRuntime::postprocess::AnomalyPostprocessorOptions options;
	options.outputName = "embedding";
	options.memoryBankPath = bankPath;
	options.embeddingDimension = 2;
	auto postprocessor = visionRuntime::postprocess::AnomalyPostprocessor::create(options);
	ASSERT_TRUE(postprocessor);

	auto result = postprocessor.value()->process(
		embeddingOutput(), {}, visionRuntime::pipeline::PipelinePacket({}));
	std::filesystem::remove(bankPath);

	ASSERT_TRUE(result);
	EXPECT_FLOAT_EQ(result->score, 4.0F);
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