#pragma once

#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace visionRuntime::camera {

enum class FileOrder {
	Lexicographical,
	LastWriteTime
};

struct FileSourceOptions {
	std::filesystem::path directory;
	std::vector<std::string> extensions{
		".bmp", ".jpeg", ".jpg", ".png", ".tif", ".tiff"};
	FileOrder order = FileOrder::Lexicographical;
	std::chrono::milliseconds frameInterval{0};
	bool recursive = false;
	bool loop = false;
};

struct ContinuousCameraSourceOptions {
	std::optional<double> frameRate;

	[[nodiscard]] bool isValid() const noexcept {
		return !frameRate || (std::isfinite(*frameRate) && *frameRate > 0.0);
	}
};

struct TimedTriggerSourceOptions {
	// Minimum interval measured from the previous successful frame arrival.
	std::chrono::milliseconds triggerInterval{100};
	std::chrono::milliseconds responseTimeout{1000};

	[[nodiscard]] bool isValid() const noexcept {
		return triggerInterval.count() >= 0 && responseTimeout.count() > 0;
	}
};

} // namespace visionRuntime::camera
