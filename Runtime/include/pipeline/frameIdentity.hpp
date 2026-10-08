#pragma once

#include "vision/frameMetadata.hpp"

#include <cstdint>
#include <optional>

namespace visionRuntime::pipeline {

struct FrameIdentity {
	std::uint64_t executionId = 0;
	std::optional<std::uint32_t> sourceId;
	vision::FrameMetadata metadata;
};

} // namespace visionRuntime::pipeline
