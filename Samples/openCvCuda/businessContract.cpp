#include "businessContract.hpp"
#include "exports/productEntry.hpp"
#include "../common/profileSupport.hpp"

namespace visionProduct {
BusinessContract::BusinessContract() {
	visionService::exports::StateSchema<CudaSummary> schema;
	schema.field("sequence", &CudaSummary::sequence)
		.field("brightFraction", &CudaSummary::brightFraction)
		.field("accepted", &CudaSummary::accepted)
		.field("valid", &CudaSummary::valid).field("error", &CudaSummary::error);
	result = exports.state({.name = "cuda.result"}, std::move(schema));
}
const BusinessContract& businessContract() {
	static const BusinessContract contract;
	return contract;
}
visionRuntime::core::Result<std::string> describeApplication(
	const visionService::profile::ProductProfile& profile) {
	if (profile.runtimeProduct != "openCvCuda" || profile.sources.size() != 1) {
		return visionSample::profileError<std::string>("openCvCuda requires its profile and one source");
	}
	return businessContract().exports.describe(profile);
}
}
