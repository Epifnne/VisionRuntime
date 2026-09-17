#pragma once

#include "config/modelPackage.hpp"

#include <filesystem>

namespace visionRuntime::config {

class ModelPackageLoader {
public:
	[[nodiscard]] static core::Result<ModelPackage> load(
		const std::filesystem::path& root);
};

} // namespace visionRuntime::config
