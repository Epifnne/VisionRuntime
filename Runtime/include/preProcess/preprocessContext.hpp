#pragma once

#include "core/result.hpp"
#include "pipeline/pipelinePacket.hpp"
#include "preprocess/preparedInput.hpp"
#include "vision/transformContext.hpp"

#include <utility>

namespace visonRuntime::preprocess {

enum class PreprocessDataState { CameraFrame, Tensor };

struct PreprocessContext {
	explicit PreprocessContext(pipeline::PipelinePacket inputPacket)
		: packet(std::move(inputPacket)) {}

	pipeline::PipelinePacket packet;
	TensorMap tensors;
	vision::TransformContext transformContext;
};

class IPreprocessNode {
public:
	virtual ~IPreprocessNode() = default;
	[[nodiscard]] virtual core::Result<void> process(PreprocessContext& context) = 0;
};

} // namespace visonRuntime::preprocess
