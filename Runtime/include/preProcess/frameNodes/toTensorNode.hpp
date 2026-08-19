#pragma once

#include "core/tensorBufferPool.hpp"
#include "preprocess/preprocessNode.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace visionRuntime::preprocess {

struct ToTensorOptions {
	std::string tensorName = "input";
	std::size_t bufferCount = 2;
	std::size_t channels = 3;
};

class ToTensor {
public:
	static constexpr auto inputState = PreprocessDataState::CameraFrame;
	static constexpr auto outputState = PreprocessDataState::Tensor;
	static constexpr bool materializes = true;

	explicit ToTensor(ToTensorOptions options = {})
		: options_(std::move(options)) {}

	[[nodiscard]] core::Result<std::unique_ptr<IPreprocessNode>> build(
		PreprocessBuildContext& context) &&;

private:
	ToTensorOptions options_;
};

class ToTensorNode final : public IPreprocessNode {
public:
	[[nodiscard]] core::Result<void> process(PreprocessContext& context) override;

private:
	friend class ToTensor;
	ToTensorNode(
		ToTensorOptions options, vision::ImageSize inputSize,
		core::TensorBufferPool pool)
		: options_(std::move(options)), inputSize_(inputSize), pool_(std::move(pool)) {}

	ToTensorOptions options_;
	vision::ImageSize inputSize_;
	core::TensorBufferPool pool_;
};

} // namespace visionRuntime::preprocess