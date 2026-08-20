#pragma once

#include "config/deploymentConfig.hpp"
#include "core/result.hpp"

#include <filesystem>

namespace visionRuntime::config {

class ConfigLoader {
public:
	[[nodiscard]] static core::Result<DeploymentConfig> loadDeployment(
		const std::filesystem::path& path);
};

} // namespace visionRuntime::config