#pragma once

#include "core/result.hpp"
#include "executor/taskHandle.hpp"
#include "pipeline/pipelinePacket.hpp"

namespace visionRuntime::executor {

enum class StopMode {
	Graceful,
	Immediate
};

template<typename ResultType>
class IPipelineExecutor {
public:
	virtual ~IPipelineExecutor() = default;

	[[nodiscard]] virtual core::Result<TaskHandle<ResultType>> submit(
		pipeline::PipelinePacket packet,
		CompletionCallback<ResultType> callback = {}) = 0;
	virtual void stop(StopMode mode = StopMode::Graceful) noexcept = 0;
};

} // namespace visionRuntime::executor