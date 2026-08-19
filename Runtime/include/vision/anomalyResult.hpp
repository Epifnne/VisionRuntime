#pragma once

#include <string_view>

namespace visionRuntime::vision {

enum class AnomalyDecision { Unknown, Ok, Ng };

[[nodiscard]] constexpr std::string_view anomalyDecisionName(
	AnomalyDecision decision) noexcept {
	switch (decision) {
	case AnomalyDecision::Unknown:
		return "UNKNOWN";
	case AnomalyDecision::Ok:
		return "OK";
	case AnomalyDecision::Ng:
		return "NG";
	}
	return "UNKNOWN";
}

struct AnomalyResult {
	float score = 0.0F;
	float threshold = 0.0F;
	AnomalyDecision decision = AnomalyDecision::Unknown;
};

} // namespace visionRuntime::vision