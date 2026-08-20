#include "config/configLoader.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

TEST(ConfigLoaderTest, LoadsExecutorPolicies) {
	const auto path = std::filesystem::temp_directory_path() /
		"vision-runtime-deployment-config.json";
	{
		std::ofstream stream(path);
		stream << R"({
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
}

TEST(ConfigLoaderTest, RejectsUnknownPerformancePolicy) {
	const auto path = std::filesystem::temp_directory_path() /
		"vision-runtime-invalid-deployment-config.json";
	{
		std::ofstream stream(path);
		stream << R"({
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