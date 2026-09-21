#include <visionruntime>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#ifndef VISION_RUNTIME_TEST_PLUGIN_DIRECTORY
#error VISION_RUNTIME_TEST_PLUGIN_DIRECTORY must be defined
#endif

namespace {

class TemporaryPresetPackage {
public:
	TemporaryPresetPackage()
		: root_(std::filesystem::temp_directory_path() /
			("vision-runtime-anomaly-preset-" +
			 std::to_string(
				 std::chrono::steady_clock::now().time_since_epoch().count()))) {
		std::filesystem::create_directories(root_ / "image");
		std::filesystem::create_directories(root_ / "package" / "artifacts");
		std::ofstream(root_ / "image" / "frame.ppm") << "P6\n1 1\n255\n\0\0\0";
		std::ofstream(root_ / "package" / "artifacts" / "model.fake") << "artifact";
		std::ofstream(root_ / "package" / "manifest.json") << R"({
			"schemaVersion": {"major": 1, "minor": 0},
			"id": "fake-anomaly",
			"version": "1.0.0",
			"inputs": [{
				"name": "images",
				"elementType": "float32",
				"layout": "nchw",
				"shape": [1, 1, 224, 224]
			}],
			"outputs": [{
				"name": "score",
				"elementType": "float32",
				"layout": "scalar",
				"shape": [1]
			}],
			"artifacts": [{
				"id": "fake-cpu",
				"backendId": "fake",
				"kind": "identity",
				"path": "artifacts/model.fake",
				"devices": ["CPU"]
			}]
		})";
	}

	~TemporaryPresetPackage() {
		std::error_code error;
		std::filesystem::remove_all(root_, error);
	}

	[[nodiscard]] const std::filesystem::path& imageDirectory() const noexcept {
		return imageDirectory_;
	}

	[[nodiscard]] const std::filesystem::path& packageDirectory() const noexcept {
		return packageDirectory_;
	}

private:
	std::filesystem::path root_;
	std::filesystem::path imageDirectory_ = root_ / "image";
	std::filesystem::path packageDirectory_ = root_ / "package";
};

TEST(AnomalyPresetTest, CreatesSessionFromPackageAndPluginDeployment) {
	using namespace visionRuntime;
	TemporaryPresetPackage package;
	auto session = runtime::presets::AnomalyPreset::create({
		.source = camera::FileFrameSourceConfig{{
			.directory = package.imageDirectory(),
			.extensions = {"ppm"},
		}},
		.model = {.packagePath = package.packageDirectory()},
		.deployment = {
			.backend = {
				.pluginDirectory = VISION_RUNTIME_TEST_PLUGIN_DIRECTORY,
				.id = "fake",
				.device = "CPU",
			},
			.executor = {
				.performancePolicy = config::PerformancePolicy::Serial,
				.queueFullPolicy = config::QueueFullPolicy::Block,
				.queueCapacity = 1,
				.stageQueueCapacity = 1,
			},
		},
	});

	ASSERT_TRUE(session) << session.status().toString();
	EXPECT_NE(session->get(), nullptr);
}

TEST(AnomalyPresetTest, AcceptsManifestWithLargerStaticBatch) {
	using namespace visionRuntime;
	class BatchPackage : public TemporaryPresetPackage {
	public:
		BatchPackage() {
			std::ofstream(packageDirectory() / "manifest.json") << R"({
				"schemaVersion": {"major": 1, "minor": 0},
				"id": "fake-anomaly",
				"version": "1.0.0",
				"inputs": [{
					"name": "images",
					"elementType": "float32",
					"layout": "nchw",
					"shape": [4, 1, 224, 224]
				}],
				"outputs": [{
					"name": "score",
					"elementType": "float32",
					"layout": "scalar",
					"shape": [1]
				}],
				"artifacts": [{
					"id": "fake-cpu",
					"backendId": "fake",
					"kind": "identity",
					"path": "artifacts/model.fake",
					"devices": ["CPU"]
				}]
			})";
		}
	};
	BatchPackage package;
	auto session = runtime::presets::AnomalyPreset::create({
		.source = camera::FileFrameSourceConfig{{
			.directory = package.imageDirectory(),
			.extensions = {"ppm"},
		}},
		.model = {.packagePath = package.packageDirectory()},
		.deployment = {
			.backend = {
				.pluginDirectory = VISION_RUNTIME_TEST_PLUGIN_DIRECTORY,
				.id = "fake",
				.device = "CPU",
			},
			.executor = {
				.performancePolicy = config::PerformancePolicy::Serial,
				.queueFullPolicy = config::QueueFullPolicy::Block,
				.queueCapacity = 1,
				.stageQueueCapacity = 1,
			},
		},
	});

	ASSERT_TRUE(session) << session.status().toString();
}

TEST(AnomalyPresetTest, RejectsManifestWithMismatchedSpatialDimensions) {
	using namespace visionRuntime;
	class BadPackage : public TemporaryPresetPackage {
	public:
		BadPackage() {
			std::ofstream(packageDirectory() / "manifest.json") << R"({
				"schemaVersion": {"major": 1, "minor": 0},
				"id": "fake-anomaly",
				"version": "1.0.0",
				"inputs": [{
					"name": "images",
					"elementType": "float32",
					"layout": "nchw",
					"shape": [1, 1, 128, 224]
				}],
				"outputs": [{
					"name": "score",
					"elementType": "float32",
					"layout": "scalar",
					"shape": [1]
				}],
				"artifacts": [{
					"id": "fake-cpu",
					"backendId": "fake",
					"kind": "identity",
					"path": "artifacts/model.fake",
					"devices": ["CPU"]
				}]
			})";
		}
	};
	BadPackage package;
	auto session = runtime::presets::AnomalyPreset::create({
		.source = camera::FileFrameSourceConfig{{
			.directory = package.imageDirectory(),
			.extensions = {"ppm"},
		}},
		.model = {.packagePath = package.packageDirectory()},
		.deployment = {
			.backend = {
				.pluginDirectory = VISION_RUNTIME_TEST_PLUGIN_DIRECTORY,
				.id = "fake",
				.device = "CPU",
			},
			.executor = {
				.performancePolicy = config::PerformancePolicy::Serial,
				.queueFullPolicy = config::QueueFullPolicy::Block,
				.queueCapacity = 1,
				.stageQueueCapacity = 1,
			},
		},
	});

	EXPECT_FALSE(session);
}

} // namespace
