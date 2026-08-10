#pragma once

#include <concepts>
#include <utility>

namespace visonRuntime::pipeline {

struct NoConstraint {
	[[nodiscard]] constexpr bool accepts(const NoConstraint&) const noexcept {
		return true;
	}
};

template<typename Constraint>
concept PortConstraint = requires(const Constraint& required, const Constraint& produced) {
	{ required.accepts(produced) } -> std::convertible_to<bool>;
};

template<typename Value, PortConstraint Constraint = NoConstraint>
class InputPort {
public:
	explicit InputPort(Constraint constraint = {})
		: constraint_(std::move(constraint)) {}

	[[nodiscard]] const Constraint& constraint() const noexcept {
		return constraint_;
	}

private:
	Constraint constraint_;
};

template<typename Value, PortConstraint Constraint = NoConstraint>
class OutputPort {
public:
	explicit OutputPort(Constraint constraint = {})
		: constraint_(std::move(constraint)) {}

	[[nodiscard]] const Constraint& constraint() const noexcept {
		return constraint_;
	}

	[[nodiscard]] bool canConnect(const InputPort<Value, Constraint>& input) const noexcept {
		return input.constraint().accepts(constraint_);
	}

private:
	Constraint constraint_;
};

} // namespace visonRuntime::pipeline