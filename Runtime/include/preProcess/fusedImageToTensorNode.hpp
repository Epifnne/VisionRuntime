#pragma once

#include "core/tensorBufferPool.hpp"
#include "preProcess/preprocessContext.hpp"

#include <cstddef>
#include <string>

namespace visonRuntime::preprocess {

struct FusedImageToTensorOptions {
	std::string inputName = "input";
	std::size_t width = 0;
	std::size_t height = 0;
	std::size_t bufferCount = 2;
};

class FusedImageToTensorNode final : public IPreprocessNode {
public:
	static constexpr auto inputState = PreprocessDataState::CameraFrame;
	static constexpr auto outputState = PreprocessDataState::Tensor;
	static constexpr bool materializes = true;

	[[nodiscard]] static core::Result<FusedImageToTensorNode> create(
		FusedImageToTensorOptions options);
	[[nodiscard]] core::Result<void> process(PreprocessContext& context) override;
	[[nodiscard]] std::size_t availableBuffers() const;

private:
	FusedImageToTensorNode(
		FusedImageToTensorOptions options,
		core::TensorBufferPool pool);

	FusedImageToTensorOptions options_;
	core::TensorBufferPool pool_;
};

} // namespace visonRuntime::preprocess
