#pragma once

#include "core/result.hpp"
#include "pipeline/pipelinePacket.hpp"

namespace visonRuntime::pipeline {

template<typename ResultType>
class IVisionPipeline {
public:
	virtual ~IVisionPipeline() = default;

	[[nodiscard]] virtual core::Result<ResultType> run(
		PipelinePacket packet) = 0;
};

} // namespace visonRuntime::pipeline