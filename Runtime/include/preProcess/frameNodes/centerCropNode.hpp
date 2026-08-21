#pragma once

#include "preProcess/frameNodes/frameNodeTypes.hpp"
#include "preProcess/preprocessNode.hpp"

#include <memory>

namespace visionRuntime::preprocess {

class CenterCrop {
public:
	static constexpr auto inputState = PreprocessDataState::CameraFrame;
	static constexpr auto outputState = PreprocessDataState::CameraFrame;
	static constexpr bool materializes = false;

	explicit CenterCrop(ImageSize size) : size_(size) {}

	[[nodiscard]] core::Result<std::unique_ptr<IPreprocessNode>> build(
		PreprocessBuildContext& context) &&;

private:
	ImageSize size_;
};

class CenterCropNode final : public IPreprocessNode {
public:
	explicit CenterCropNode(ImageSize size) : size_(size) {}
	[[nodiscard]] core::Result<void> process(PreprocessContext& context) override;

private:
	ImageSize size_;
};

} // namespace visionRuntime::preprocess