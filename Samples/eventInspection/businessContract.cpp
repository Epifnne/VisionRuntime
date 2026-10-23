#include "businessContract.hpp"
#include "exports/productEntry.hpp"

namespace visionProduct {
namespace {
template<typename T>
visionService::exports::StateSchema<T> identitySchema() {
	visionService::exports::StateSchema<T> schema;
	schema.field("workpieceId", &T::workpieceId).field("inspectionId", &T::inspectionId)
		.field("captureId", &T::captureId);
	return schema;
}
}
BusinessContract::BusinessContract() {
	auto brightness = identitySchema<Brightness>();
	brightness.field("value", &Brightness::value).field("accepted", &Brightness::accepted);
	top = exports.state({.name = "view.top"}, std::move(brightness));
	auto edges = identitySchema<Edges>();
	edges.field("transitions", &Edges::transitions);
	side = exports.state({.name = "view.side"}, std::move(edges));
	visionService::exports::StateSchema<Summary> schema;
	schema.field("workpieceId", &Summary::workpieceId).field("inspectionId", &Summary::inspectionId)
		.field("decision", &Summary::decision).field("okCount", &Summary::okCount)
		.field("ngCount", &Summary::ngCount).field("faultCount", &Summary::faultCount)
		.field("views", &Summary::views);
	result = exports.state({.name = "workpiece.result"}, std::move(schema));
}
const BusinessContract& businessContract() {
	static const BusinessContract contract;
	return contract;
}
visionRuntime::core::Result<std::string> describeApplication(
	const visionService::profile::ProductProfile& profile) {
	if (profile.runtimeProduct != "eventInspection" || profile.sources.size() != 2 ||
		profile.sources[0].id != "top" || profile.sources[1].id != "side" ||
		profile.sources[0].type != "camera" || profile.sources[1].type != "camera" ||
		profile.sources[0].camera.mode != "eventTrigger" ||
		profile.sources[1].camera.mode != "eventTrigger") {
		return visionRuntime::core::Result<std::string>::failure(visionRuntime::core::Status::error(
			visionRuntime::core::StatusCode::InvalidArgument,
			"eventInspection requires top and side event-trigger camera slots"));
	}
	return businessContract().exports.describe(profile);
}
}
