#pragma once

#include "config/executorConfig.hpp"

#include <filesystem>
#include <string>

namespace visionRuntime::config {

struct BackendDeploymentConfig {
	std::filesystem::path pluginDirectory;
	std::string id;
	std::string device;
};

struct DeploymentConfig {
	BackendDeploymentConfig backend{};
	ExecutorConfig executor{};
};

} // namespace visionRuntime::config