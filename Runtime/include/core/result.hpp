#pragma once

#include "core/status.hpp"

#include <cassert>
#include <concepts>
#include <type_traits>
#include <utility>
#include <variant>

namespace visonRuntime::core {

template<typename T>
class [[nodiscard]] Result {
public:
	static_assert(!std::is_void_v<T>);
	static_assert(!std::is_reference_v<T>);

	template<typename U = T>
		requires std::constructible_from<T, U&&>
	[[nodiscard]] static Result success(U&& value) {
		return Result(ValueTag{}, std::forward<U>(value));
	}

	[[nodiscard]] static Result failure(Status status) {
		assert(!status.isOk());
		return Result(ErrorTag{}, std::move(status));
	}

	[[nodiscard]] bool hasValue() const noexcept {
		return storage_.index() == 0;
	}

	[[nodiscard]] explicit operator bool() const noexcept {
		return hasValue();
	}

	[[nodiscard]] T& value() & {
		assert(hasValue());
		return std::get<0>(storage_);
	}

	[[nodiscard]] const T& value() const & {
		assert(hasValue());
		return std::get<0>(storage_);
	}

	[[nodiscard]] T&& value() && {
		assert(hasValue());
		return std::get<0>(std::move(storage_));
	}

	[[nodiscard]] const T&& value() const && {
		assert(hasValue());
		return std::get<0>(std::move(storage_));
	}

	[[nodiscard]] T& operator*() & {
		return value();
	}

	[[nodiscard]] const T& operator*() const & {
		return value();
	}

	[[nodiscard]] T* operator->() {
		return &value();
	}

	[[nodiscard]] const T* operator->() const {
		return &value();
	}

	[[nodiscard]] const Status& status() const & noexcept {
		if (hasValue()) {
			return okStatus_;
		}
		return std::get<1>(storage_);
	}

private:
	struct ValueTag {};
	struct ErrorTag {};

	template<typename U>
	explicit Result(ValueTag, U&& value)
		: storage_(std::in_place_index<0>, std::forward<U>(value)) {}

	explicit Result(ErrorTag, Status status)
		: storage_(std::in_place_index<1>, std::move(status)) {}

	inline static const Status okStatus_ = Status::ok();
	std::variant<T, Status> storage_;
};

template<>
class [[nodiscard]] Result<void> {
public:
	[[nodiscard]] static Result success() noexcept {
		return Result(Status::ok());
	}

	[[nodiscard]] static Result failure(Status status) {
		assert(!status.isOk());
		return Result(std::move(status));
	}

	[[nodiscard]] bool hasValue() const noexcept {
		return status_.isOk();
	}

	[[nodiscard]] explicit operator bool() const noexcept {
		return hasValue();
	}

	void value() const noexcept {
		assert(hasValue());
	}

	[[nodiscard]] const Status& status() const & noexcept {
		return status_;
	}

private:
	explicit Result(Status status)
		: status_(std::move(status)) {}

	Status status_;
};

} // namespace visonRuntime::core