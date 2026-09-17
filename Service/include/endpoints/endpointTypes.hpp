/**
 * @file endpointTypes.hpp
 * @brief Shared value types for the visionService endpoint registry.
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

namespace visionService::endpoints {

enum class EndpointKind {
	Parameter,
	Command,
	State,
	Stream
};

enum class ParameterType {
	Decimal,
	Integer,
	Flag,
	Text
};

enum class AccessLevel {
	Operator,
	Engineer,
	Administrator
};

using ParameterValue = std::variant<double, std::int64_t, bool, std::string>;

[[nodiscard]] constexpr std::string_view endpointKindName(
	EndpointKind kind) noexcept {
	switch (kind) {
	case EndpointKind::Parameter:
		return "parameter";
	case EndpointKind::Command:
		return "command";
	case EndpointKind::State:
		return "state";
	case EndpointKind::Stream:
		return "stream";
	}
	return "unknown";
}

[[nodiscard]] constexpr std::string_view parameterTypeName(
	ParameterType type) noexcept {
	switch (type) {
	case ParameterType::Decimal:
		return "decimal";
	case ParameterType::Integer:
		return "integer";
	case ParameterType::Flag:
		return "flag";
	case ParameterType::Text:
		return "text";
	}
	return "unknown";
}

[[nodiscard]] constexpr std::string_view accessLevelName(
	AccessLevel level) noexcept {
	switch (level) {
	case AccessLevel::Operator:
		return "operator";
	case AccessLevel::Engineer:
		return "engineer";
	case AccessLevel::Administrator:
		return "administrator";
	}
	return "unknown";
}

[[nodiscard]] constexpr ParameterType parameterTypeOf(
	const ParameterValue& value) noexcept {
	switch (value.index()) {
	case 0:
		return ParameterType::Decimal;
	case 1:
		return ParameterType::Integer;
	case 2:
		return ParameterType::Flag;
	default:
		return ParameterType::Text;
	}
}

} // namespace visionService::endpoints
