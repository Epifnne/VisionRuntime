#include "config/configLoader.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

TEST(ConfigLoaderTest, RejectsNegativeAndFractionalQueueCapacities) {
	const auto path = std::filesystem::temp_directory_path() / "vision-runtime-negative-queue.json";
	for (const auto capacity : {"-1", "1.5"}) {
		for (const auto field : {"queueCapacity", "stageQueueCapacity"}) {
			std::ofstream stream(path);
			stream << R"({"schemaVersion":{"major":1},
				"backend":{"pluginDirectory":"plugins","id":"openvino","device":"CPU"},
				"executor":{"performancePolicy":"serial","queueFullPolicy":"block",
				"queueCapacity":1,")" << field << "\":" << capacity << "}}";
			stream.close();
			auto loaded = visionRuntime::config::ConfigLoader::loadDeployment(path);
			ASSERT_FALSE(loaded);
			EXPECT_EQ(loaded.status().code(), visionRuntime::core::StatusCode::InvalidArgument);
		}
	}
	std::filesystem::remove(path);
}

TEST(ConfigLoaderTest, LoadsUtf8PluginDirectory) {
	const auto path = std::filesystem::temp_directory_path() / "vision-runtime-utf8-deployment.json";
	std::ofstream stream(path);
	stream << R"({"schemaVersion":{"major":1},
		"backend":{"pluginDirectory":"\u63d2\u4ef6","id":"openvino","device":"CPU"},
		"executor":{"performancePolicy":"serial","queueFullPolicy":"block","queueCapacity":1}})";
	stream.close();
	auto loaded = visionRuntime::config::ConfigLoader::loadDeployment(path);
	std::filesystem::remove(path);
	ASSERT_TRUE(loaded) << loaded.status().toString();
	EXPECT_EQ(loaded->backend.pluginDirectory.filename(), std::filesystem::path(u8"\u63d2\u4ef6"));
}

TEST(ConfigLoaderTest, LoadsExecutorPolicies) {
	const auto path = std::filesystem::temp_directory_path() /
		"vision-runtime-deployment-config.json";
	{
		std::ofstream stream(path);
		stream << R"({
			"schemaVersion": {"major": 1, "minor": 0},
			"backend": {
				"pluginDirectory": "plugins",
				"id": "tensorrt",
				"device": "0"
			},
			"executor": {
				"performancePolicy": "pipelineParallel",
				"queueFullPolicy": "block",
				"queueCapacity": 8,
				"stageQueueCapacity": 2
			}
		})";
	}

	auto loaded = visionRuntime::config::ConfigLoader::loadDeployment(path);
	std::filesystem::remove(path);

	ASSERT_TRUE(loaded);
	EXPECT_EQ(loaded->executor.performancePolicy,
		visionRuntime::config::PerformancePolicy::PipelineParallel);
	EXPECT_EQ(loaded->executor.queueFullPolicy,
		visionRuntime::config::QueueFullPolicy::Block);
	EXPECT_EQ(loaded->executor.queueCapacity, 8U);
	EXPECT_EQ(loaded->executor.stageQueueCapacity, 2U);
	EXPECT_EQ(loaded->backend.id, "tensorrt");
	EXPECT_EQ(loaded->backend.device, "0");
	EXPECT_EQ(loaded->backend.pluginDirectory,
		(std::filesystem::absolute(path).parent_path() / "plugins").lexically_normal());
}

TEST(ConfigLoaderTest, RejectsUnknownPerformancePolicy) {
	const auto path = std::filesystem::temp_directory_path() /
		"vision-runtime-invalid-deployment-config.json";
	{
		std::ofstream stream(path);
		stream << R"({
			"schemaVersion": {"major": 1, "minor": 0},
			"backend": {
				"pluginDirectory": "plugins",
				"id": "openvino",
				"device": "CPU"
			},
			"executor": {
				"performancePolicy": "unlimited",
				"queueFullPolicy": "drop",
				"queueCapacity": 8
			}
		})";
	}

	auto loaded = visionRuntime::config::ConfigLoader::loadDeployment(path);
	std::filesystem::remove(path);

	EXPECT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(),
		visionRuntime::core::StatusCode::InvalidArgument);
}

TEST(ConfigLoaderTest, RejectsUnknownSchemaMajor) {
	const auto path = std::filesystem::temp_directory_path() /
		"vision-runtime-unsupported-deployment-config.json";
	{
		std::ofstream stream(path);
		stream << R"({
			"schemaVersion": {"major": 2, "minor": 0},
			"backend": {
				"pluginDirectory": "plugins",
				"id": "openvino",
				"device": "CPU"
			},
			"executor": {
				"performancePolicy": "serial",
				"queueFullPolicy": "block",
				"queueCapacity": 1
			}
		})";
	}

	auto loaded = visionRuntime::config::ConfigLoader::loadDeployment(path);
	std::filesystem::remove(path);

	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(),
		visionRuntime::core::StatusCode::Unsupported);
}

TEST(ConfigLoaderTest, RejectsEmptyPluginDirectory) {
	const auto path = std::filesystem::temp_directory_path() /
		"vision-runtime-empty-plugin-directory.json";
	{
		std::ofstream stream(path);
		stream << R"({
			"schemaVersion": {"major": 1, "minor": 0},
			"backend": {
				"pluginDirectory": "",
				"id": "openvino",
				"device": "CPU"
			},
			"executor": {
				"performancePolicy": "serial",
				"queueFullPolicy": "block",
				"queueCapacity": 1
			}
		})";
	}

	auto loaded = visionRuntime::config::ConfigLoader::loadDeployment(path);
	std::filesystem::remove(path);

	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(),
		visionRuntime::core::StatusCode::InvalidArgument);
}