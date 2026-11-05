#pragma once

#include "core/result.hpp"
#include "pipeline/pipelinePacket.hpp"
#include "preprocess/preparedInput.hpp"
#include "preprocess/nodeTiming.hpp"

namespace visionRuntime::preprocess {

class IPreprocessor {
public:
	virtual ~IPreprocessor() = default;

	[[nodiscard]] virtual core::Result<PreparedInput> process(
		pipeline::PipelinePacket packet) = 0;

	[[nodiscard]] virtual core::Result<PreparedInput> processWithTiming(
		pipeline::PipelinePacket packet, const NodeTimingObserver&) {
		return process(std::move(packet));
	}
};

} // namespace visionRuntime::preprocess