#pragma once

#include "executor/taskHandle.hpp"
#include "pipeline/frameIdentity.hpp"

#include <functional>
#include <optional>

namespace visionRuntime::executor {

enum class FrameOutcome {
	Completed,
	Dropped,
	Rejected
};

template<typename ResultType>
using FrameOutcomeCallback = std::function<void(
	FrameOutcome,
	const pipeline::FrameIdentity&,
	std::optional<TaskId>,
	const core::Result<ResultType>&)>;

} // namespace visionRuntime::executor
