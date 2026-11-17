#include "businessContract.hpp"
#include "exports/productEntry.hpp"
#include "../common/profileSupport.hpp"

namespace visionProduct {
namespace {

visionService::exports::StateSchema<Measurement> measurementSchema() {
	visionService::exports::StateSchema<Measurement> schema;
	schema.field("sequence", &Measurement::sequence)
		.field("brightness", &Measurement::brightness, "mean grayscale value", "gray")
		.field("brightFraction", &Measurement::brightFraction, "fraction above the CUDA threshold")
		.field("accepted", &Measurement::accepted)
		.field("valid", &Measurement::valid)
		.field("error", &Measurement::error);
	return schema;
}

} // namespace

BusinessContract::BusinessContract(bool cuda)
	: minimum(exports.parameter<double>({
		.name = cuda ? "measure.minimumBrightFraction" : "measure.minimumBrightness",
		.description = cuda ? "minimum accepted bright fraction" : "minimum accepted mean brightness",
		.writable = true, .minimum = 0.0, .maximum = cuda ? 1.0 : 255.0})),
	  result(exports.state(
		{.name = "measure.result", .description = "latest OpenCV measurement"},
		measurementSchema())),
	  reset(exports.command(
		{.name = "measure.reset", .description = "clear the displayed measurement"})) {}

const BusinessContract& businessContract(bool cuda) {
	static const BusinessContract cpuContract(false);
	static const BusinessContract cudaContract(true);
	return cuda ? cudaContract : cpuContract;
}

visionRuntime::core::Result<bool> measurementUsesCuda(
	const visionService::profile::ProductProfile& profile) {
	using Result = visionRuntime::core::Result<bool>;
	try {
		const auto document = nlohmann::json::parse(profile.documentJson);
		const auto mode = document.value("measurement", nlohmann::json::object()).value("mode", "cpu");
		if (mode != "cpu" && mode != "cuda") {
			return visionSample::profileError<bool>("measurement.mode must be cpu or cuda");
		}
		return Result::success(mode == "cuda");
	} catch (const nlohmann::json::exception& exception) {
		return visionSample::profileError<bool>(
			std::string("measurement configuration: ") + exception.what());
	}
}

visionRuntime::core::Result<std::string> describeApplication(
	const visionService::profile::ProductProfile& profile) {
	if (profile.runtimeProduct != "businessExports" || profile.sources.size() != 1) {
		return visionRuntime::core::Result<std::string>::failure(
			visionRuntime::core::Status::error(visionRuntime::core::StatusCode::InvalidArgument,
				"businessExports requires its product profile and one source"));
	}
	auto cuda = measurementUsesCuda(profile);
	if (!cuda) {
		return visionRuntime::core::Result<std::string>::failure(cuda.status());
	}
	return businessContract(cuda.value()).exports.describe(profile);
}

} // namespace visionProduct
