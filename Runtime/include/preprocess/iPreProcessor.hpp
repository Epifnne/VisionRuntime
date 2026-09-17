#pragma once

#include "core/result.hpp"
#include "pipeline/pipelinePacket.hpp"
#include "preprocess/preparedInput.hpp"

namespace visionRuntime::preprocess {

class IPreprocessor {
public:
	virtual ~IPreprocessor() = default;

	[[nodiscard]] virtual core::Result<PreparedInput> process(
		pipeline::PipelinePacket packet) = 0;
};

} // namespace visionRuntime::preprocess