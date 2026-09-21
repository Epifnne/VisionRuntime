/**
 * @file profileLoader.hpp
 * @brief Parses a product profile JSON into a ProductProfile.
 */

#pragma once

#include "core/result.hpp"
#include "profile/productProfile.hpp"

#include <filesystem>

namespace visionService::profile {

class ProfileLoader {
public:
	ProfileLoader() = delete;

	[[nodiscard]] static visionRuntime::core::Result<ProductProfile> load(
		const std::filesystem::path& path);
};

} // namespace visionService::profile
