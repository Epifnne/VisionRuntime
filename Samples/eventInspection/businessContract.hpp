#pragma once

#include "exports/exportContract.hpp"

namespace visionProduct {

struct Brightness {
	double value = 0;
	bool accepted = false;
	std::uint64_t workpieceId = 0;
	std::uint64_t inspectionId = 0;
	std::uint64_t captureId = 0;
};
struct Edges {
	int transitions = 0;
	std::uint64_t workpieceId = 0;
	std::uint64_t inspectionId = 0;
	std::uint64_t captureId = 0;
};
struct Summary {
	std::uint64_t workpieceId = 0;
	std::uint64_t inspectionId = 0;
	std::string decision = "NONE";
	std::size_t okCount = 0;
	std::size_t ngCount = 0;
	std::size_t faultCount = 0;
	std::string views;
};
struct BusinessContract {
	visionService::exports::ExportContract exports;
	visionService::exports::StateDefinition<Brightness> top;
	visionService::exports::StateDefinition<Edges> side;
	visionService::exports::StateDefinition<Summary> result;
	BusinessContract();
};
const BusinessContract& businessContract();

} // namespace visionProduct
