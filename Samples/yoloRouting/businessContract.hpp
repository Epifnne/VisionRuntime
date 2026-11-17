#pragma once

#include "exports/exportContract.hpp"

namespace visionProduct {

struct RoutingSummary {
	std::uint64_t sequence = 0;
	std::size_t objects = 0;
	std::size_t defects = 0;
	std::size_t texts = 0;
	bool simulatedDetection = true;
	bool valid = false;
	std::string error;
};

struct BusinessContract {
	visionService::exports::ExportContract exports;
	visionService::exports::StateDefinition<RoutingSummary> result;
	visionService::exports::StreamDefinition annotated;
	BusinessContract();
};

const BusinessContract& businessContract();

} // namespace visionProduct
