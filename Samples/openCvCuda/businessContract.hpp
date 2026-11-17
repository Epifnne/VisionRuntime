#pragma once

#include "exports/exportContract.hpp"

namespace visionProduct {
struct CudaSummary {
	std::uint64_t sequence = 0;
	double brightFraction = 0;
	bool accepted = false;
	bool valid = false;
	std::string error;
};

struct BusinessContract {
	visionService::exports::ExportContract exports;
	visionService::exports::StateDefinition<CudaSummary> result;
	BusinessContract();
};

const BusinessContract& businessContract();
}
