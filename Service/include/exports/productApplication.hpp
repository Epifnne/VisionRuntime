#pragma once

#include "exports/businessExports.hpp"
#include "profile/productProfile.hpp"
#include "session/inspectionSession.hpp"

namespace visionService {

struct ProductApplication {
	std::unique_ptr<session::IInspectionSession> session;
	exports::BusinessExports exports;
	std::string expectedManifest;
};

using ProductApplicationFactory = std::function<
	visionRuntime::core::Result<ProductApplication>(const profile::ProductProfile&)>;

} // namespace visionService
