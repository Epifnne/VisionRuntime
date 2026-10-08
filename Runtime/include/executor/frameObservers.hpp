#pragma once

#include "camera/sourceFault.hpp"
#include "executor/frameOutcome.hpp"
#include "vision/frame.hpp"

#include <functional>

namespace visionRuntime::executor {

template<typename ResultType>
struct FrameObservers {
	FrameOutcomeCallback<ResultType> result{};
	std::function<void(const camera::SourceFault&)> sourceFault{};
	std::function<void(std::uint32_t, const vision::Frame&)> frame{};
};

} // namespace visionRuntime::executor
