#include "backends/tensorRtBackend.hpp"

#include "core/dataType.hpp"
#include "core/status.hpp"
#include "core/tensor.hpp"
#include "memory/cpuAllocator.hpp"
#include "preProcess/preparedInput.hpp"
#include "runtime/presets/anomalyPreset.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <vector>

namespace {

using visionRuntime::backends::TensorRtBackend;
using visionRuntime::core::DataType;
using visionRuntime::core::StatusCode;
using visionRuntime::core::Tensor;
using visionRuntime::memory::CpuAllocator;
using visionRuntime::preprocess::TensorMap;
using visionRuntime::runtime::presets::AnomalyPreset;

TEST(TensorRtBackendTest, RejectsMissingEngine) {
	auto backend = TensorRtBackend::create({
		.enginePath = "missing-tensorrt-engine.plan",
	});

	ASSERT_FALSE(backend);
	EXPECT_EQ(backend.status().code(), StatusCode::NotFound);
}

TEST(TensorRtBackendTest, RejectsInvalidSerializedEngine) {
	const auto enginePath =
		std::filesystem::temp_directory_path() / "vision-runtime-invalid.engine";
	{
		std::ofstream engine(enginePath, std::ios::binary);
		engine << "not a TensorRT engine";
	}

	auto backend = TensorRtBackend::create({
		.enginePath = enginePath,
	});
	std::error_code ignored;
	std::filesystem::remove(enginePath, ignored);

	ASSERT_FALSE(backend);
	EXPECT_EQ(backend.status().code(), StatusCode::BackendError);
}

TEST(TensorRtBackendTest, RunsDynamicShapeEngine) {
	const auto* enginePath = std::getenv("VISION_TENSORRT_TEST_ENGINE");
	if (enginePath == nullptr) {
		GTEST_SKIP() << "VISION_TENSORRT_TEST_ENGINE is not set";
	}
	auto backend = TensorRtBackend::create({
		.enginePath = enginePath,
		.inputName = "image",
		.outputName = "score",
	});
	ASSERT_TRUE(backend) << backend.status().toString();

	CpuAllocator allocator;
	auto input = allocator.allocateTensor(DataType::Float32, {1, 1, 2, 3});
	ASSERT_TRUE(input) << input.status().toString();
	constexpr std::array values{1.0F, -2.0F, 3.5F, 4.0F, 0.0F, -6.25F};
	std::copy(values.begin(), values.end(), static_cast<float*>(input->data()));
	TensorMap inputs;
	inputs.emplace("image", std::move(input).value());

	auto outputs = backend.value()->infer(inputs);
	ASSERT_TRUE(outputs) << outputs.status().toString();
	const auto output = outputs->find("score");
	ASSERT_NE(output, outputs->end());
	EXPECT_EQ(output->second.shape().dimensions(),
		(std::vector<std::int64_t>{1, 1, 2, 3}));
	const auto* outputData = static_cast<const float*>(output->second.data());
	EXPECT_TRUE(std::equal(values.begin(), values.end(), outputData));

	auto largerInput = allocator.allocateTensor(DataType::Float32, {2, 1, 4, 5});
	ASSERT_TRUE(largerInput) << largerInput.status().toString();
	std::vector<float> largerValues(largerInput->elementCount());
	std::iota(largerValues.begin(), largerValues.end(), -10.0F);
	std::copy(largerValues.begin(), largerValues.end(),
		static_cast<float*>(largerInput->data()));
	TensorMap largerInputs;
	largerInputs.emplace("image", std::move(largerInput).value());
	auto largerOutputs = backend.value()->infer(largerInputs);
	ASSERT_TRUE(largerOutputs) << largerOutputs.status().toString();
	const auto largerOutput = largerOutputs->find("score");
	ASSERT_NE(largerOutput, largerOutputs->end());
	EXPECT_EQ(largerOutput->second.shape().dimensions(),
		(std::vector<std::int64_t>{2, 1, 4, 5}));
	const auto* largerOutputData =
		static_cast<const float*>(largerOutput->second.data());
	EXPECT_TRUE(std::equal(
		largerValues.begin(), largerValues.end(), largerOutputData));

	auto outOfProfileInput = allocator.allocateTensor(
		DataType::Float32, {2, 1, 5, 5});
	ASSERT_TRUE(outOfProfileInput) << outOfProfileInput.status().toString();
	TensorMap outOfProfileInputs;
	outOfProfileInputs.emplace("image", std::move(outOfProfileInput).value());
	auto outOfProfile = backend.value()->infer(outOfProfileInputs);
	ASSERT_FALSE(outOfProfile);
	EXPECT_EQ(outOfProfile.status().code(), StatusCode::InvalidArgument);
}

TEST(TensorRtBackendTest, AnomalyPresetSelectsTensorRtBackend) {
	const auto sourceDirectory =
		std::filesystem::temp_directory_path() / "vision-runtime-tensorrt-preset";
	std::filesystem::create_directories(sourceDirectory);
	{
		std::ofstream image(sourceDirectory / "frame.png", std::ios::binary);
		image << "placeholder";
	}
	AnomalyPreset::Options options;
	options.source = visionRuntime::camera::FileFrameSourceConfig{{
		.directory = sourceDirectory,
	}};
	options.model.path = sourceDirectory / "missing.engine";

	auto session = AnomalyPreset::create(std::move(options));
	std::error_code ignored;
	std::filesystem::remove_all(sourceDirectory, ignored);

	ASSERT_FALSE(session);
	EXPECT_EQ(session.status().code(), StatusCode::NotFound);
	EXPECT_NE(session.status().message().find("TensorRT engine"),
		std::string_view::npos);
}

} // namespace
