#pragma once
#include "service/visionService.hpp"

namespace visionSample {
visionRuntime::core::Result<std::unique_ptr<visionService::session::IInspectionSession>>
createProductRuntime(const visionService::profile::ProductProfile& profile,
	visionService::endpoints::EndpointRegistry& registry);
}
