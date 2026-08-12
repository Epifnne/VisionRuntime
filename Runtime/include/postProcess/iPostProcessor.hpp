#pragma once

#include "core/result.hpp"
#include "preprocess/preparedInput.hpp"
#include "vision/transformContext.hpp"

namespace visonRuntime::postprocess {

template<typename ResultType>
class IPostprocessor {
public:
	virtual ~IPostprocessor() = default;

	[[nodiscard]] virtual core::Result<ResultType> process(
		const preprocess::TensorMap& outputs,
		const vision::TransformContext& transformContext,
		const pipeline::PipelinePacket& packet) = 0;
};

} // namespace visonRuntime::postprocess