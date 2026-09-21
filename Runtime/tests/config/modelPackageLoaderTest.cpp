#include "config/modelPackageLoader.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

using visionRuntime::config::BackendDeploymentConfig;
using visionRuntime::config::ModelPackageLoader;
using visionRuntime::core::StatusCode;

class PackageDirectory {
public:
	explicit PackageDirectory(std::string_view name)
		: path_(std::filesystem::temp_directory_path() / name) {
		std::filesystem::remove_all(path_);
		std::filesystem::create_directories(path_ / "artifacts");
	}

	~PackageDirectory() { std::filesystem::remove_all(path_); }

	[[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

	void writeManifest(std::string_view artifacts, std::uint32_t schemaMajor = 1,
		std::string_view inputShape = "[1, 1, 224, 224]") {
		std::ofstream stream(path_ / "manifest.json");
		stream << R"({
			"schemaVersion": {"major": )" << schemaMajor << R"(, "minor": 0},
			"id": "anomaly-model",
			"version": "1.0.0",
			"inputs": [{
				"name": "images",
				"elementType": "float32",
				"layout": "nchw",
				"shape": )" << inputShape << R"(
			}],
			"outputs": [{
				"name": "score",
				"elementType": "float32",
				"layout": "scalar",
				"shape": [1]
			}],
			"artifacts": )" << artifacts << "\n}";
	}

	void writeArtifact(std::string_view name) {
		std::ofstream(path_ / "artifacts" / name) << "artifact";
	}

private:
	std::filesystem::path path_;
};

TEST(ModelPackageLoaderTest, LoadsAndSelectsUniqueArtifact) {
	PackageDirectory package("vision-runtime-valid-model-package");
	package.writeArtifact("model.engine");
	package.writeManifest(R"([{
		"id": "tensorrt-fp32",
		"backendId": "tensorrt",
		"kind": "engine",
		"path": "artifacts/model.engine",
		"devices": ["0"],
		"options": {"optimizationProfile": 2}
	}])");

	auto loaded = ModelPackageLoader::load(package.path());
	ASSERT_TRUE(loaded) << loaded.status().toString();
	EXPECT_EQ(loaded->id(), "anomaly-model");
	EXPECT_EQ(loaded->manifest().inputs.front().name, "images");

	auto selected = loaded->selectArtifact({
		.pluginDirectory = "plugins",
		.id = "tensorrt",
		.device = "0",
	});
	ASSERT_TRUE(selected) << selected.status().toString();
	EXPECT_EQ(selected->artifactKind, "engine");
	EXPECT_EQ(selected->artifactPath,
		std::filesystem::canonical(package.path() / "artifacts/model.engine"));
	EXPECT_EQ(selected->optionsJson, R"({"optimizationProfile":2})");
	EXPECT_EQ(selected->device, "0");
}

TEST(ModelPackageLoaderTest, RejectsUnknownSchemaMajor) {
	PackageDirectory package("vision-runtime-unsupported-model-package");
	package.writeManifest("[]", 2);

	auto loaded = ModelPackageLoader::load(package.path());

	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), StatusCode::Unsupported);
}

TEST(ModelPackageLoaderTest, LoadsUtf8ArtifactPath) {
	PackageDirectory package("vision-runtime-utf8-package");
	const auto filename = std::filesystem::path(u8"\u6a21\u578b.engine");
	std::ofstream(package.path() / "artifacts" / filename) << "artifact";
	package.writeManifest(R"([{
		"id":"utf8", "backendId":"tensorrt", "kind":"engine",
		"path":"artifacts/\u6a21\u578b.engine", "devices":["0"]
	}])");
	auto loaded = ModelPackageLoader::load(package.path());
	ASSERT_TRUE(loaded) << loaded.status().toString();
	EXPECT_EQ(loaded->artifacts().front().path.filename(), filename);
}

TEST(ModelPackageLoaderTest, RejectsNegativeAndFractionalTensorDimensions) {
	PackageDirectory package("vision-runtime-negative-shape-model-package");
	for (const auto shape : {"[1, -1, 224, 224]", "[1, 1.5, 224, 224]"}) {
		package.writeManifest("[]", 1, shape);
		auto loaded = ModelPackageLoader::load(package.path());
		ASSERT_FALSE(loaded) << shape;
		EXPECT_EQ(loaded.status().code(), StatusCode::InvalidArgument);
	}
}

TEST(ModelPackageLoaderTest, RejectsZeroTensorDimension) {
	PackageDirectory package("vision-runtime-invalid-shape-model-package");
	std::ofstream stream(package.path() / "manifest.json");
	stream << R"({
		"schemaVersion": {"major": 1, "minor": 0},
		"id": "invalid-shape",
		"version": "1.0.0",
		"inputs": [{
			"name": "images",
			"elementType": "float32",
			"layout": "nchw",
			"shape": [1, 1, 0, 224]
		}],
		"outputs": [{
			"name": "score",
			"elementType": "float32",
			"layout": "scalar",
			"shape": [1]
		}],
		"artifacts": []
	})";
	stream.close();

	auto loaded = ModelPackageLoader::load(package.path());

	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), StatusCode::InvalidArgument);
}

TEST(ModelPackageLoaderTest, RejectsArtifactOutsidePackage) {
	PackageDirectory package("vision-runtime-escaping-model-package");
	const auto outsidePath = package.path().parent_path() / "outside.engine";
	std::ofstream(outsidePath) << "artifact";
	package.writeManifest(R"([{
		"id": "escape",
		"backendId": "tensorrt",
		"kind": "engine",
		"path": "../outside.engine",
		"devices": ["0"]
	}])");

	auto loaded = ModelPackageLoader::load(package.path());
	std::filesystem::remove(outsidePath);

	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), StatusCode::InvalidArgument);
}

TEST(ModelPackageLoaderTest, RejectsArtifactsDirectorySymlinkOutsidePackage) {
	PackageDirectory package("vision-runtime-linked-package");
	PackageDirectory outside("vision-runtime-linked-package-outside");
	outside.writeArtifact("model.engine");
	std::filesystem::remove(package.path() / "artifacts");
	std::error_code status;
	std::filesystem::create_directory_symlink(
		outside.path() / "artifacts", package.path() / "artifacts", status);
	if (status) {
		GTEST_SKIP() << "directory symlink unavailable: " << status.message();
	}
	package.writeManifest(R"([{
		"id":"escape", "backendId":"tensorrt", "kind":"engine",
		"path":"artifacts/model.engine", "devices":["0"]
	}])");
	auto loaded = ModelPackageLoader::load(package.path());
	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), StatusCode::InvalidArgument);
}

TEST(ModelPackageLoaderTest, RejectsEmptyOrNonArrayPorts) {
	PackageDirectory package("vision-runtime-empty-ports-package");
	for (const auto ports : {"[]", "null", "{}"}) {
		std::ofstream stream(package.path() / "manifest.json");
		stream << R"({"schemaVersion":{"major":1},"id":"empty","version":"1",
			"inputs":)" << ports << R"(,"outputs":[],"artifacts":[]})";
		stream.close();
		auto loaded = ModelPackageLoader::load(package.path());
		ASSERT_FALSE(loaded);
		EXPECT_EQ(loaded.status().code(), StatusCode::InvalidArgument);
	}
}

TEST(ModelPackageLoaderTest, RejectsArtifactOutsideArtifactsDirectory) {
	PackageDirectory package("vision-runtime-misplaced-artifact-package");
	std::ofstream(package.path() / "model.engine") << "artifact";
	package.writeManifest(R"([{
		"id": "misplaced",
		"backendId": "tensorrt",
		"kind": "engine",
		"path": "model.engine",
		"devices": ["0"]
	}])");

	auto loaded = ModelPackageLoader::load(package.path());

	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), StatusCode::InvalidArgument);
}

TEST(ModelPackageLoaderTest, RejectsMissingArtifactFile) {
	PackageDirectory package("vision-runtime-missing-artifact-package");
	package.writeManifest(R"([{
		"id": "missing",
		"backendId": "openvino",
		"kind": "ir",
		"path": "artifacts/missing.xml",
		"devices": ["CPU"]
	}])");

	auto loaded = ModelPackageLoader::load(package.path());

	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), StatusCode::NotFound);
}

TEST(ModelPackageLoaderTest, RejectsDuplicateArtifactIds) {
	PackageDirectory package("vision-runtime-duplicate-artifact-package");
	package.writeArtifact("first.engine");
	package.writeArtifact("second.engine");
	package.writeManifest(R"([{
		"id": "duplicate",
		"backendId": "tensorrt",
		"kind": "engine",
		"path": "artifacts/first.engine",
		"devices": ["0"]
	}, {
		"id": "duplicate",
		"backendId": "tensorrt",
		"kind": "engine",
		"path": "artifacts/second.engine",
		"devices": ["1"]
	}])");

	auto loaded = ModelPackageLoader::load(package.path());

	ASSERT_FALSE(loaded);
	EXPECT_EQ(loaded.status().code(), StatusCode::InvalidArgument);
}

TEST(ModelPackageLoaderTest, RejectsEmptyArtifactContractFields) {
	PackageDirectory package("vision-runtime-empty-artifact-fields-package");
	package.writeArtifact("model.engine");
	const std::string_view artifacts[] = {
		R"([{"id":"","backendId":"tensorrt","kind":"engine","path":"artifacts/model.engine","devices":["0"]}])",
		R"([{"id":"model","backendId":"","kind":"engine","path":"artifacts/model.engine","devices":["0"]}])",
		R"([{"id":"model","backendId":"tensorrt","kind":"","path":"artifacts/model.engine","devices":["0"]}])",
		R"([{"id":"model","backendId":"tensorrt","kind":"engine","path":"artifacts/model.engine","devices":[]}])",
		R"([{"id":"model","backendId":"tensorrt","kind":"engine","path":"artifacts/model.engine","devices":["0","0"]}])",
	};
	for (const auto artifact : artifacts) {
		package.writeManifest(artifact);
		auto loaded = ModelPackageLoader::load(package.path());
		ASSERT_FALSE(loaded) << artifact;
		EXPECT_EQ(loaded.status().code(), StatusCode::InvalidArgument);
	}
}

TEST(ModelPackageLoaderTest, RejectsMissingDeploymentMatch) {
	PackageDirectory package("vision-runtime-no-match-model-package");
	package.writeArtifact("model.engine");
	package.writeManifest(R"([{
		"id": "gpu-zero",
		"backendId": "tensorrt",
		"kind": "engine",
		"path": "artifacts/model.engine",
		"devices": ["0"]
	}])");
	auto loaded = ModelPackageLoader::load(package.path());
	ASSERT_TRUE(loaded) << loaded.status().toString();

	auto selected = loaded->selectArtifact(BackendDeploymentConfig{
		.id = "openvino",
		.device = "CPU",
	});

	ASSERT_FALSE(selected);
	EXPECT_EQ(selected.status().code(), StatusCode::NotFound);
}

TEST(ModelPackageLoaderTest, RejectsMultipleDeploymentMatches) {
	PackageDirectory package("vision-runtime-multiple-match-model-package");
	package.writeArtifact("first.engine");
	package.writeArtifact("second.engine");
	package.writeManifest(R"([{
		"id": "first",
		"backendId": "tensorrt",
		"kind": "engine",
		"path": "artifacts/first.engine",
		"devices": ["0"]
	}, {
		"id": "second",
		"backendId": "tensorrt",
		"kind": "engine",
		"path": "artifacts/second.engine",
		"devices": ["0"]
	}])");
	auto loaded = ModelPackageLoader::load(package.path());
	ASSERT_TRUE(loaded) << loaded.status().toString();

	auto selected = loaded->selectArtifact(BackendDeploymentConfig{
		.id = "tensorrt",
		.device = "0",
	});

	ASSERT_FALSE(selected);
	EXPECT_EQ(selected.status().code(), StatusCode::InvalidArgument);
}

} // namespace
