#include "backends/pluginInferenceBackend.hpp"
#include "config/modelPackageLoader.hpp"
#include "memory/cpuAllocator.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <numeric>

namespace {

using visionRuntime::backends::PluginBackendOptions;
using visionRuntime::backends::PluginInferenceBackend;
using visionRuntime::config::ModelPackageLoader;
using visionRuntime::core::StatusCode;

PluginBackendOptions options(const visionRuntime::config::ModelPackage& package) {
	auto selected = package.selectArtifact({
		.id = TEST_BACKEND_ID, .device = TEST_BACKEND_DEVICE,
	}).value();
	return {
		.pluginPath = TEST_PLUGIN_PATH,
		.backendId = TEST_BACKEND_ID,
		.artifactPath = selected.artifactPath,
		.device = selected.device,
		.optionsJson = selected.optionsJson,
		.artifactKind = selected.artifactKind,
		.inputName = package.manifest().inputs.front().name,
		.outputName = package.manifest().outputs.front().name,
	};
}

TEST(VendorPluginTest, RejectsMissingArtifact) {
	auto backend = PluginInferenceBackend::create({
		.pluginPath = TEST_PLUGIN_PATH,
		.backendId = TEST_BACKEND_ID,
		.artifactPath = "missing-model-artifact",
		.device = TEST_BACKEND_DEVICE,
		.optionsJson = "{}",
		.artifactKind = TEST_ARTIFACT_KIND,
		.inputName = "image",
		.outputName = "score",
	});
	ASSERT_FALSE(backend);
	EXPECT_EQ(backend.status().code(), StatusCode::NotFound) << backend.status().toString();
}

TEST(VendorPluginTest, RejectsUnsupportedArtifactKind) {
	auto backend = PluginInferenceBackend::create({
		.pluginPath = TEST_PLUGIN_PATH,
		.backendId = TEST_BACKEND_ID,
		.optionsJson = "{}",
		.artifactKind = "unknown",
		.inputName = "image",
		.outputName = "score",
	});
	ASSERT_FALSE(backend);
	EXPECT_EQ(backend.status().code(), StatusCode::Unsupported) << backend.status().toString();
}

TEST(VendorPluginTest, RunsPackageWithDynamicShapesAndOwnedOutputs) {
	const auto* root = std::getenv(TEST_PACKAGE_ENV);
	if (root == nullptr) {
		GTEST_SKIP() << TEST_PACKAGE_ENV << " is not set";
	}
	auto package = ModelPackageLoader::load(root);
	ASSERT_TRUE(package) << package.status().toString();
	auto backend = PluginInferenceBackend::create(options(package.value()));
	ASSERT_TRUE(backend) << backend.status().toString();

	visionRuntime::memory::CpuAllocator allocator;
	std::vector<visionRuntime::preprocess::TensorMap> retainedOutputs;
	for (const auto& dimensions : {std::vector<int64_t>{1, 1, 2, 3},
		std::vector<int64_t>{2, 1, 4, 5}}) {
		auto input = allocator.allocateTensor(visionRuntime::core::DataType::Float32,
			visionRuntime::core::TensorShape(dimensions)).value();
		auto* data = static_cast<float*>(input.data());
		std::iota(data, data + input.elementCount(), -3.0F);
		visionRuntime::preprocess::TensorMap inputs;
		inputs.emplace("image", std::move(input));
		auto outputs = backend.value()->infer(inputs);
		ASSERT_TRUE(outputs) << outputs.status().toString();
		ASSERT_EQ(outputs->size(), 1U);
		ASSERT_TRUE(outputs->contains("score"));
		EXPECT_EQ(outputs->at("score").shape().dimensions(), dimensions);
		retainedOutputs.push_back(std::move(outputs).value());
	}
	backend.value().reset();
	for (const auto& outputs : retainedOutputs) {
		const auto& tensor = outputs.at("score");
		const auto* data = static_cast<const float*>(tensor.data());
		for (std::size_t index = 0; index < tensor.elementCount(); ++index) {
			EXPECT_FLOAT_EQ(data[index], -3.0F + static_cast<float>(index));
		}
	}
}

TEST(VendorPluginTest, RejectsModelPortMismatch) {
	const auto* root = std::getenv(TEST_PACKAGE_ENV);
	if (root == nullptr) {
		GTEST_SKIP() << TEST_PACKAGE_ENV << " is not set";
	}
	auto package = ModelPackageLoader::load(root);
	ASSERT_TRUE(package) << package.status().toString();
	auto settings = options(package.value());
	settings.inputName = "wrong-input-name";
	auto backend = PluginInferenceBackend::create(std::move(settings));
	ASSERT_FALSE(backend);
}

TEST(VendorPluginTest, RejectsInvalidBackendOptions) {
	const auto* root = std::getenv(TEST_PACKAGE_ENV);
	if (root == nullptr) {
		GTEST_SKIP() << TEST_PACKAGE_ENV << " is not set";
	}
	auto package = ModelPackageLoader::load(root);
	ASSERT_TRUE(package) << package.status().toString();
	auto settings = options(package.value());
#if defined(TEST_TENSORRT)
	settings.optionsJson = R"({"optimizationProfile":-1})";
#else
	settings.optionsJson = R"({"inferenceThreads":-1})";
#endif
	auto backend = PluginInferenceBackend::create(std::move(settings));
	ASSERT_FALSE(backend);
	EXPECT_EQ(backend.status().code(), StatusCode::InvalidArgument);
}

#if defined(TEST_TENSORRT)
TEST(VendorPluginTest, RejectsShapeOutsideProfile) {
	const auto* root = std::getenv(TEST_PACKAGE_ENV);
	if (root == nullptr) {
		GTEST_SKIP() << TEST_PACKAGE_ENV << " is not set";
	}
	auto package = ModelPackageLoader::load(root);
	ASSERT_TRUE(package) << package.status().toString();
	auto backend = PluginInferenceBackend::create(options(package.value()));
	ASSERT_TRUE(backend) << backend.status().toString();
	visionRuntime::memory::CpuAllocator allocator;
	visionRuntime::preprocess::TensorMap inputs;
	inputs.emplace("image", allocator.allocateTensor(
		visionRuntime::core::DataType::Float32, {2, 1, 5, 5}).value());
	auto result = backend.value()->infer(inputs);
	ASSERT_FALSE(result);
	EXPECT_EQ(result.status().code(), StatusCode::InvalidArgument);
}
#endif

} // namespace