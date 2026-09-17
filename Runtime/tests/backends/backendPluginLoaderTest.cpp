#include "backends/pluginInferenceBackend.hpp"

#include "core/dataType.hpp"
#include "memory/cpuAllocator.hpp"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>

#ifndef VISION_RUNTIME_FAKE_PLUGIN_PATH
#error VISION_RUNTIME_FAKE_PLUGIN_PATH must be defined
#endif
#ifndef VISION_RUNTIME_MISSING_ENTRY_PLUGIN_PATH
#error VISION_RUNTIME_MISSING_ENTRY_PLUGIN_PATH must be defined
#endif
#ifndef VISION_RUNTIME_INCOMPATIBLE_PLUGIN_PATH
#error VISION_RUNTIME_INCOMPATIBLE_PLUGIN_PATH must be defined
#endif

namespace {

using visionRuntime::backends::PluginInferenceBackend;
using visionRuntime::core::DataType;
using visionRuntime::core::StatusCode;
using visionRuntime::memory::CpuAllocator;
using visionRuntime::preprocess::TensorMap;

TEST(BackendPluginLoaderTest, LoadsPluginAndRunsInference) {
	auto backend = PluginInferenceBackend::create({
		.pluginPath = VISION_RUNTIME_FAKE_PLUGIN_PATH,
		.backendId = "fake",
	});
	ASSERT_TRUE(backend) << backend.status().toString();

	CpuAllocator allocator;
	auto input = allocator.allocateTensor(DataType::Float32, {1, 3});
	ASSERT_TRUE(input) << input.status().toString();
	constexpr std::array values{1.5F, -2.0F, 4.25F};
	std::copy(values.begin(), values.end(), static_cast<float*>(input->data()));
	TensorMap inputs;
	inputs.emplace("input", std::move(input).value());

	auto outputs = backend.value()->infer(inputs);
	ASSERT_TRUE(outputs) << outputs.status().toString();
	const auto output = outputs->find("output");
	ASSERT_NE(output, outputs->end());
	EXPECT_EQ(output->second.shape().dimensions(),
		(std::vector<std::int64_t>{1, 3}));
	const auto* data = static_cast<const float*>(output->second.data());
	EXPECT_FLOAT_EQ(data[0], 3.0F);
	EXPECT_FLOAT_EQ(data[1], -4.0F);
	EXPECT_FLOAT_EQ(data[2], 8.5F);
}

TEST(BackendPluginLoaderTest, RejectsBackendIdMismatch) {
	auto backend = PluginInferenceBackend::create({
		.pluginPath = VISION_RUNTIME_FAKE_PLUGIN_PATH,
		.backendId = "other",
	});

	ASSERT_FALSE(backend);
	EXPECT_EQ(backend.status().code(), StatusCode::InvalidArgument);
}

TEST(BackendPluginLoaderTest, RejectsSuccessWithoutBackendHandle) {
	auto backend = PluginInferenceBackend::create({
		.pluginPath = VISION_RUNTIME_FAKE_PLUGIN_PATH,
		.backendId = "fake",
		.optionsJson = "nullHandle",
	});

	ASSERT_FALSE(backend);
	EXPECT_EQ(backend.status().code(), StatusCode::Internal);
}

TEST(BackendPluginLoaderTest, PassesArtifactPathAsUtf8) {
	auto backend = PluginInferenceBackend::create({
		.pluginPath = VISION_RUNTIME_FAKE_PLUGIN_PATH,
		.backendId = "fake",
		.artifactPath = std::filesystem::path(u8"\u6a21\u578b.engine"),
		.optionsJson = "verifyUtf8Path",
	});

	ASSERT_TRUE(backend) << backend.status().toString();
}

TEST(BackendPluginLoaderTest, RejectsPluginWithoutEntryPoint) {
	auto backend = PluginInferenceBackend::create({
		.pluginPath = VISION_RUNTIME_MISSING_ENTRY_PLUGIN_PATH,
		.backendId = "missing",
	});

	ASSERT_FALSE(backend);
	EXPECT_EQ(backend.status().code(), StatusCode::InvalidArgument);
}

TEST(BackendPluginLoaderTest, RejectsIncompatibleAbi) {
	auto backend = PluginInferenceBackend::create({
		.pluginPath = VISION_RUNTIME_INCOMPATIBLE_PLUGIN_PATH,
		.backendId = "incompatible",
	});

	ASSERT_FALSE(backend);
	EXPECT_EQ(backend.status().code(), StatusCode::Unsupported);
}

TEST(BackendPluginLoaderTest, MapsPluginInferenceError) {
	auto backend = PluginInferenceBackend::create({
		.pluginPath = VISION_RUNTIME_FAKE_PLUGIN_PATH,
		.backendId = "fake",
	});
	ASSERT_TRUE(backend) << backend.status().toString();

	TensorMap inputs;
	auto outputs = backend.value()->infer(inputs);

	ASSERT_FALSE(outputs);
	EXPECT_EQ(outputs.status().code(), StatusCode::InvalidArgument);
	EXPECT_EQ(outputs.status().message(), "fake backend requires one input");
}

TEST(BackendPluginLoaderTest, BoundsUnterminatedPluginError) {
	auto backend = PluginInferenceBackend::create({
		.pluginPath = VISION_RUNTIME_FAKE_PLUGIN_PATH,
		.backendId = "fake",
		.optionsJson = "unterminatedError",
	});
	ASSERT_TRUE(backend) << backend.status().toString();

	TensorMap inputs;
	auto outputs = backend.value()->infer(inputs);

	ASSERT_FALSE(outputs);
	EXPECT_EQ(outputs.status().code(), StatusCode::InvalidArgument);
	EXPECT_EQ(outputs.status().message().size(), 1023U);
}

} // namespace
