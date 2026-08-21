#pragma once

#include "core/result.hpp"
#include "pipeline/pipelinePacket.hpp"
#include "preProcess/preparedInput.hpp"
#include "vision/transformContext.hpp"

#include <optional>
#include <utility>

namespace visionRuntime::preprocess {

enum class PreprocessDataState { CameraFrame, Tensor };

struct PreprocessContext {
	explicit PreprocessContext(pipeline::PipelinePacket inputPacket)
		: packet(std::move(inputPacket)) {}

	[[nodiscard]] const vision::Frame* currentFrame() const noexcept {
		return workingFrame ? &*workingFrame : packet.cameraFrame();
	}

	void setWorkingFrame(vision::Frame frame) {
		workingFrame = std::move(frame);
	}

	pipeline::PipelinePacket packet;
	std::optional<vision::Frame> workingFrame;
	TensorMap tensors;
	vision::TransformContext transformContext;
};

class IPreprocessNode {
public:
	virtual ~IPreprocessNode() = default;
	[[nodiscard]] virtual core::Result<void> process(PreprocessContext& context) = 0;
};

} // namespace visionRuntime::preprocess
