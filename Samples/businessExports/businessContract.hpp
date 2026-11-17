#pragma once

#include "exports/exportContract.hpp"

namespace visionProduct {

struct Measurement {
	std::uint64_t sequence = 0;
	double brightness = 0;
	double brightFraction = 0;
	bool accepted = false;
	bool valid = false;
	std::string error;
};

struct BusinessContract {
	visionService::exports::ExportContract exports;
	visionService::exports::ParameterDefinition<double> minimum;
	visionService::exports::StateDefinition<Measurement> result;
	visionService::exports::CommandDefinition reset;

	explicit BusinessContract(bool cuda);
};

[[nodiscard]] const BusinessContract& businessContract(bool cuda = false);
[[nodiscard]] visionRuntime::core::Result<bool> measurementUsesCuda(
	const visionService::profile::ProductProfile& profile);

} // namespace visionProduct
