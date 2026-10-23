#include "businessContract.hpp"
#include "exports/productEntry.hpp"

namespace visionProduct {
namespace {

visionService::exports::StateSchema<Measurement> measurementSchema() {
	visionService::exports::StateSchema<Measurement> schema;
	schema.field("sequence", &Measurement::sequence)
		.field("brightness", &Measurement::brightness, "mean grayscale value", "gray")
		.field("accepted", &Measurement::accepted)
		.field("valid", &Measurement::valid)
		.field("error", &Measurement::error);
	return schema;
}

} // namespace

BusinessContract::BusinessContract()
	: minimum(exports.parameter<double>({
		.name = "measure.minimumBrightness",
		.description = "minimum accepted mean brightness",
		.writable = true, .minimum = 0.0, .maximum = 255.0})),
	  result(exports.state(
		{.name = "measure.result", .description = "latest OpenCV measurement"},
		measurementSchema())),
	  reset(exports.command(
		{.name = "measure.reset", .description = "clear the displayed measurement"})) {}

const BusinessContract& businessContract() {
	static const BusinessContract contract;
	return contract;
}

visionRuntime::core::Result<std::string> describeApplication(
	const visionService::profile::ProductProfile& profile) {
	if (profile.runtimeProduct != "businessExports" || profile.sources.size() != 1 ||
		profile.sources.front().type != "directory") {
		return visionRuntime::core::Result<std::string>::failure(
			visionRuntime::core::Status::error(visionRuntime::core::StatusCode::InvalidArgument,
				"businessExports requires its product profile and one directory source"));
	}
	return businessContract().exports.describe(profile);
}

} // namespace visionProduct
