#include "profile/profileLoader.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

using visionService::profile::ProfileLoader;

class TemporaryProfile {
public:
	TemporaryProfile()
		: root_(std::filesystem::temp_directory_path() /
			("vision-service-profile-" +
			 std::to_string(
				 std::chrono::steady_clock::now().time_since_epoch().count()))) {
		std::filesystem::create_directories(root_);
	}

	~TemporaryProfile() {
		std::error_code error;
		std::filesystem::remove_all(root_, error);
	}

	[[nodiscard]] std::filesystem::path write(
		std::string_view content) const {
		const auto path = root_ / "profile.json";
		std::ofstream(path) << content;
		return path;
	}

	[[nodiscard]] const std::filesystem::path& root() const noexcept {
		return root_;
	}

private:
	std::filesystem::path root_;
};

TEST(ProfileLoaderTest, ParsesDirectoryAndCameraSources) {
	TemporaryProfile fixture;
	const auto path = fixture.write(R"({
		"schemaVersion": {"major": 1, "minor": 0},
		"id": "o-ring-inspection",
		"name": "O-Ring Inspection",
		"sources": [
			{
				"id": "cam0",
				"type": "camera",
				"role": "top view",
				"vendor": "hikrobot",
				"serialNumber": "SN001",
				"ipAddress": "192.168.1.100",
				"pixelFormat": "gray8",
				"exposureMicroseconds": 12000.0,
				"gain": 6.0,
				"mode": "continuous"
			},
			{
				"id": "files",
				"type": "directory",
				"directory": "images",
				"extensions": [".png"],
				"loop": false,
				"frameIntervalMilliseconds": 40
			}
		],
		"model": {
			"packagePath": "package",
			"backendId": "fake",
			"device": "CPU",
			"pluginDirectory": "plugins"
		},
		"pipeline": {
			"resizeShortSide": 256,
			"cropWidth": 224,
			"cropHeight": 224,
			"mean": [0.449],
			"standardDeviation": [0.226],
			"threshold": 2.0,
			"maxBatchSize": 4,
			"flushTimeoutMilliseconds": 20,
			"queueCapacity": 32,
			"queueFullPolicy": "block"
		},
		"endpoints": [
			{"name": "camera.cam0.exposureMicroseconds", "access": "engineer"},
			{"name": "session.start"}
		],
		"ui": {"pages": [{"id": "monitor"}]}
	})");

	auto profile = ProfileLoader::load(path);
	ASSERT_TRUE(profile) << profile.status().toString();
	EXPECT_EQ(profile->id, "o-ring-inspection");
	ASSERT_EQ(profile->sources.size(), 2U);

	const auto& camera = profile->sources[0];
	EXPECT_EQ(camera.type, "camera");
	EXPECT_EQ(camera.camera.vendor, "hikrobot");
	EXPECT_EQ(camera.camera.serialNumber, "SN001");
	EXPECT_EQ(camera.camera.ipAddress, "192.168.1.100");
	EXPECT_EQ(camera.camera.pixelFormat, "gray8");
	ASSERT_TRUE(camera.camera.exposureMicroseconds.has_value());
	EXPECT_DOUBLE_EQ(*camera.camera.exposureMicroseconds, 12000.0);
	EXPECT_EQ(camera.camera.mode, "continuous");

	const auto& directory = profile->sources[1];
	EXPECT_EQ(directory.type, "directory");
	EXPECT_EQ(directory.directory.directory,
		(fixture.root() / "images").lexically_normal());
	EXPECT_EQ(directory.directory.extensions.size(), 1U);
	EXPECT_FALSE(directory.directory.loop);
	EXPECT_EQ(directory.directory.frameInterval, std::chrono::milliseconds(40));

	EXPECT_EQ(profile->model.backendId, "fake");
	EXPECT_EQ(profile->pipeline.maxBatchSize, 4U);
	EXPECT_DOUBLE_EQ(profile->pipeline.threshold, 2.0);
	EXPECT_EQ(profile->pipeline.queueCapacity, 32U);
	EXPECT_EQ(profile->pipeline.queueFullPolicy, "block");
	ASSERT_EQ(profile->endpoints.size(), 2U);
	EXPECT_EQ(profile->endpoints[0].access, "engineer");
	EXPECT_EQ(profile->endpoints[1].access, "operator");
	EXPECT_NE(profile->uiJson.find("monitor"), std::string::npos);
}

TEST(ProfileLoaderTest, AppliesPipelineDefaults) {
	TemporaryProfile fixture;
	const auto path = fixture.write(R"({
		"schemaVersion": {"major": 1},
		"id": "minimal",
		"sources": [{"id": "files", "type": "directory", "directory": "."}],
		"model": {
			"packagePath": ".",
			"backendId": "fake",
			"device": "CPU",
			"pluginDirectory": "plugins"
		}
	})");

	auto profile = ProfileLoader::load(path);
	ASSERT_TRUE(profile) << profile.status().toString();
	EXPECT_EQ(profile->pipeline.resizeShortSide, 256U);
	EXPECT_EQ(profile->pipeline.cropWidth, 224U);
	EXPECT_EQ(profile->pipeline.maxBatchSize, 0U);
	EXPECT_EQ(profile->pipeline.queueCapacity, 16U);
	EXPECT_TRUE(profile->endpoints.empty());
	EXPECT_TRUE(profile->uiJson.empty());
}

TEST(ProfileLoaderTest, RejectsUnsupportedSchemaMajor) {
	TemporaryProfile fixture;
	const auto path = fixture.write(R"({
		"schemaVersion": {"major": 2},
		"id": "future",
		"sources": [{"id": "files", "type": "directory", "directory": "."}],
		"model": {
			"packagePath": ".",
			"backendId": "fake",
			"device": "CPU",
			"pluginDirectory": "plugins"
		}
	})");

	auto profile = ProfileLoader::load(path);
	ASSERT_FALSE(profile);
	EXPECT_EQ(profile.status().code(),
		visionRuntime::core::StatusCode::Unsupported);
}

TEST(ProfileLoaderTest, RejectsUnknownSourceType) {
	TemporaryProfile fixture;
	const auto path = fixture.write(R"({
		"schemaVersion": {"major": 1},
		"id": "bad-source",
		"sources": [{"id": "mystery", "type": "network"}],
		"model": {
			"packagePath": ".",
			"backendId": "fake",
			"device": "CPU",
			"pluginDirectory": "plugins"
		}
	})");

	auto profile = ProfileLoader::load(path);
	ASSERT_FALSE(profile);
	EXPECT_EQ(profile.status().code(),
		visionRuntime::core::StatusCode::InvalidArgument);
}

} // namespace
