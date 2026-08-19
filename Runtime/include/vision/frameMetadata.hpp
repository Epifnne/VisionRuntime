/**
 * @file frameMetadata.hpp
 * @author epifnne
 * @date 2026-08-10
 * @brief Defines sequence and timestamp metadata associated with captured frames.
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace visionRuntime::vision {

struct FrameMetadata {
	std::uint64_t sequenceNumber = 0;
	std::chrono::steady_clock::time_point capturedAt{};
	std::optional<std::chrono::nanoseconds> hardwareTimestamp;
};

} // namespace visionRuntime::vision