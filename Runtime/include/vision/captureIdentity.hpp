#pragma once

#include <cstdint>

namespace visionRuntime::vision {

struct CaptureIdentity {
	std::uint64_t workpieceId = 0;
	std::uint64_t inspectionId = 0;
	std::uint64_t captureId = 0;
	std::uint32_t viewId = 0;

	bool operator==(const CaptureIdentity&) const = default;
};

} // namespace visionRuntime::vision
