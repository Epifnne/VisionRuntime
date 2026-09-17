#pragma once

#include "config/deploymentConfig.hpp"
#include "config/modelManifest.hpp"
#include "core/result.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace visionRuntime::config {

struct ModelArtifact {
	std::string id;
	std::string backendId;
	std::string kind;
	std::filesystem::path path;
	std::vector<std::string> devices;
	std::string optionsJson;
};

struct SelectedModelArtifact {
	std::filesystem::path artifactPath;
	std::string artifactKind;
	std::string optionsJson;
	std::string device;
};

class ModelPackage {
public:
	ModelPackage(
		std::filesystem::path root,
		std::string id,
		std::string version,
		ModelManifest manifest,
		std::vector<ModelArtifact> artifacts);

	[[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
	[[nodiscard]] const std::string& id() const noexcept { return id_; }
	[[nodiscard]] const std::string& version() const noexcept { return version_; }
	[[nodiscard]] const ModelManifest& manifest() const noexcept { return manifest_; }
	[[nodiscard]] const std::vector<ModelArtifact>& artifacts() const noexcept {
		return artifacts_;
	}

	[[nodiscard]] core::Result<SelectedModelArtifact> selectArtifact(
		const BackendDeploymentConfig& deployment) const;

private:
	std::filesystem::path root_;
	std::string id_;
	std::string version_;
	ModelManifest manifest_;
	std::vector<ModelArtifact> artifacts_;
};

} // namespace visionRuntime::config
