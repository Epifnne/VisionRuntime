#pragma once

#include "core/status.hpp"

#include <cstddef>
#include <functional>
#include <string>

namespace visionRuntime::preprocess {

struct NodeTiming {
	std::size_t index = 0;
	std::string name;
	double milliseconds = 0.0;
	core::Status status;
};

using NodeTimingObserver = std::function<void(const NodeTiming& timing)>;

} // namespace visionRuntime::preprocess
