#include "businessContract.hpp"
#include "exports/productEntry.hpp"
#include "../common/profileSupport.hpp"

namespace visionProduct {

BusinessContract::BusinessContract() {
	visionService::exports::StateSchema<RoutingSummary> schema;
	schema.field("sequence", &RoutingSummary::sequence)
		.field("objects", &RoutingSummary::objects)
		.field("defects", &RoutingSummary::defects)
		.field("texts", &RoutingSummary::texts)
		.field("simulatedDetection", &RoutingSummary::simulatedDetection)
		.field("valid", &RoutingSummary::valid).field("error", &RoutingSummary::error);
	result = exports.state({.name = "routing.result"}, std::move(schema));
	annotated = exports.stream({.name = "routing.annotated",
		.description = "owned annotated image", .sourceId = 1});
}

const BusinessContract& businessContract() {
	static const BusinessContract contract;
	return contract;
}

visionRuntime::core::Result<std::string> describeApplication(
	const visionService::profile::ProductProfile& profile) {
	if (profile.runtimeProduct != "yoloRouting" || profile.sources.size() != 1) {
		return visionSample::profileError<std::string>("yoloRouting requires its profile and one source");
	}
	return businessContract().exports.describe(profile);
}

} // namespace visionProduct
