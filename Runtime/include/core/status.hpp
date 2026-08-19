#pragma once

#include <cassert>
#include <string>
#include <string_view>
#include <utility>

namespace visionRuntime::core {

enum class StatusCode {
	Ok,
	Cancelled,
	InvalidArgument,
	InvalidState,
	NotFound,
	AlreadyExists,
	Unsupported,
	ShapeMismatch,
	ResourceExhausted,
	QueueFull,
	Unavailable,
	DeadlineExceeded,
	DataLoss,
	BackendError,
	Internal
};

[[nodiscard]] constexpr std::string_view statusCodeName(StatusCode code) noexcept {
	switch (code) {
	case StatusCode::Ok:
		return "Ok";
	case StatusCode::Cancelled:
		return "Cancelled";
	case StatusCode::InvalidArgument:
		return "InvalidArgument";
	case StatusCode::InvalidState:
		return "InvalidState";
	case StatusCode::NotFound:
		return "NotFound";
	case StatusCode::AlreadyExists:
		return "AlreadyExists";
	case StatusCode::Unsupported:
		return "Unsupported";
	case StatusCode::ShapeMismatch:
		return "ShapeMismatch";
	case StatusCode::ResourceExhausted:
		return "ResourceExhausted";
	case StatusCode::QueueFull:
		return "QueueFull";
	case StatusCode::Unavailable:
		return "Unavailable";
	case StatusCode::DeadlineExceeded:
		return "DeadlineExceeded";
	case StatusCode::DataLoss:
		return "DataLoss";
	case StatusCode::BackendError:
		return "BackendError";
	case StatusCode::Internal:
		return "Internal";
	}

	return "Unknown";
}

class Status {
public:
	Status() = default;

	[[nodiscard]] static Status ok() noexcept {
		return {};
	}

	[[nodiscard]] static Status error(StatusCode code, std::string message) {
		assert(code != StatusCode::Ok);
		assert(!message.empty());
		return Status(code, std::move(message));
	}

	[[nodiscard]] bool isOk() const noexcept {
		return code_ == StatusCode::Ok;
	}

	[[nodiscard]] explicit operator bool() const noexcept {
		return isOk();
	}

	[[nodiscard]] StatusCode code() const noexcept {
		return code_;
	}

	[[nodiscard]] std::string_view message() const noexcept {
		return message_;
	}

	[[nodiscard]] Status withContext(std::string context) const & {
		Status status = *this;
		status.prependContext(std::move(context));
		return status;
	}

	[[nodiscard]] Status withContext(std::string context) && {
		prependContext(std::move(context));
		return std::move(*this);
	}

	[[nodiscard]] std::string toString() const {
		if (isOk()) {
			return std::string(statusCodeName(code_));
		}

		std::string text(statusCodeName(code_));
		text += ": ";
		if (!context_.empty()) {
			text += context_;
			text += ": ";
		}
		text += message_;
		return text;
	}

private:
	Status(StatusCode code, std::string message)
		: code_(code), message_(std::move(message)) {}

	void prependContext(std::string context) {
		assert(!isOk());
		assert(!context.empty());
		if (isOk() || context.empty()) {
			return;
		}

		if (context_.empty()) {
			context_ = std::move(context);
			return;
		}

		context += ": ";
		context += context_;
		context_ = std::move(context);
	}

	StatusCode code_ = StatusCode::Ok;
	std::string message_;
	std::string context_;
};

} // namespace visionRuntime::core