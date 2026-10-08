#pragma once

#include "core/status.hpp"

#include <cstdint>
#include <optional>

namespace visionRuntime::camera {

struct SourceFault {
	std::uint32_t sourceId = 0;
	core::Status status;
	bool terminal = false;
	std::uint64_t missingFrames = 0;
	std::optional<std::uint64_t> previousSequence{};
	std::optional<std::uint64_t> currentSequence{};
};

class FrameSequenceTracker {
public:
	[[nodiscard]] std::uint64_t observe(std::uint64_t sequence) noexcept {
		const auto missing = previous_ && sequence > *previous_
			? sequence - *previous_ - 1 : 0;
		previous_ = sequence;
		return missing;
	}

	[[nodiscard]] std::optional<std::uint64_t> previous() const noexcept {
		return previous_;
	}

private:
	std::optional<std::uint64_t> previous_;
};

} // namespace visionRuntime::camera
