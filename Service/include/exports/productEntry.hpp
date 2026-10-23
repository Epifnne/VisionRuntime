#pragma once

#include "exports/productApplication.hpp"

namespace visionProduct {

[[nodiscard]] visionRuntime::core::Result<std::string>
describeApplication(const visionService::profile::ProductProfile& profile);

[[nodiscard]] visionRuntime::core::Result<visionService::ProductApplication>
createApplication(const visionService::profile::ProductProfile& profile);

} // namespace visionProduct
