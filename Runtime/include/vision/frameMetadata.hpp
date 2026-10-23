/**
 * @file frameMetadata.hpp
 * @author epifnne
 * @date 2026-08-10
 * @brief Defines sequence and timestamp metadata associated with captured frames.
 */

#pragma once

#include "vision/captureIdentity.hpp"

#include <chrono>
#include <cstdint>
#include <optional>

namespace visionRuntime::vision {

struct FrameMetadata {
	std::uint64_t sequenceNumber = 0;
	std::chrono::steady_clock::time_point capturedAt{};
	std::optional<std::chrono::nanoseconds> hardwareTimestamp;
	std::optional<CaptureIdentity> capture;
};

} // namespace visionRuntime::vision