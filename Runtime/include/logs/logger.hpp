#pragma once

#include "core/status.hpp"

#include <string_view>

namespace visionRuntime::logs {

enum class Level {
	Debug,
	Info,
	Warning,
	Error
};

/**
 * Emits one record to the framework log sink (stderr by default). The
 * concrete logging library is an implementation detail and never appears in
 * this facade.
 */
void log(Level level, std::string_view message) noexcept;

inline void debug(std::string_view message) noexcept {
	log(Level::Debug, message);
}

inline void info(std::string_view message) noexcept {
	log(Level::Info, message);
}

inline void warning(std::string_view message) noexcept {
	log(Level::Warning, message);
}

inline void error(std::string_view message) noexcept {
	log(Level::Error, message);
}

/// Reports a failed Status to the framework log; Ok statuses are ignored.
/// Framework entry points call this before returning a failure so that
/// callers never need to print errors themselves.
inline void report(const core::Status& status) {
	if (!status) {
		error(status.toString());
	}
}

} // namespace visionRuntime::logs