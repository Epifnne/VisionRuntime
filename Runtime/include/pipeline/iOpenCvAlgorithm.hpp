#pragma once

#include "core/result.hpp"
#include "pipeline/pipelinePacket.hpp"

namespace visonRuntime::pipeline {

template<typename ResultType>
class IOpenCvAlgorithm {
public:
	virtual ~IOpenCvAlgorithm() = default;

	[[nodiscard]] virtual core::Result<ResultType> process(
		const PipelinePacket& packet) = 0;
};

} // namespace visonRuntime::pipeline