#include "logs/logger.hpp"

#include <spdlog/spdlog.h>

#if defined(_WIN32)
#include <spdlog/sinks/wincolor_sink.h>
#else
#include <spdlog/sinks/ansicolor_sink.h>
#endif

#include <memory>
#include <utility>

namespace visionRuntime::logs {
namespace {

#if defined(_WIN32)
using StderrColorSink = spdlog::sinks::wincolor_stderr_sink_mt;
#else
using StderrColorSink = spdlog::sinks::ansicolor_stderr_sink_mt;
#endif

[[nodiscard]] spdlog::logger& defaultLogger() {
	static const auto instance = [] {
		// NOTE: this vendored spdlog copy is trimmed: stderr_color_sinks.h is
		// absent and only the platform color sinks above have compiled symbols.
		auto sink = std::make_shared<StderrColorSink>();
		auto logger = std::make_shared<spdlog::logger>(
			"visionruntime", std::move(sink));
		logger->set_pattern("[%H:%M:%S.%e] [%l] %v");
		logger->set_level(spdlog::level::debug);
		logger->flush_on(spdlog::level::err);
		return logger;
	}();
	return *instance;
}

[[nodiscard]] constexpr spdlog::level::level_enum toSpdlogLevel(
	Level level) noexcept {
	switch (level) {
	case Level::Debug:
		return spdlog::level::debug;
	case Level::Info:
		return spdlog::level::info;
	case Level::Warning:
		return spdlog::level::warn;
	case Level::Error:
		return spdlog::level::err;
	}
	return spdlog::level::info;
}

} // namespace

void log(Level level, std::string_view message) noexcept {
	try {
		defaultLogger().log(toSpdlogLevel(level),
			spdlog::string_view_t(message.data(), message.size()));
	} catch (...) {
		// Logging is called from noexcept contexts and must never fail them.
	}
}

} // namespace visionRuntime::logs
