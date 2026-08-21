#pragma once

#include "core/tensorBufferPool.hpp"
#include "preProcess/frameNodes/frameNodeTypes.hpp"
#include "preProcess/preprocessNode.hpp"

#include <cstddef>
#include <memory>

namespace visionRuntime::preprocess {

struct ResizeOptions {
	std::size_t shortSide = 0;
	std::size_t maxLongSide = 4096;
	std::size_t bufferCount = 2;
};

class Resize {
public:
	static constexpr auto inputState = PreprocessDataState::CameraFrame;
	static constexpr auto outputState = PreprocessDataState::CameraFrame;
	static constexpr bool materializes = false;

	[[nodiscard]] static Resize shortSide(
		std::size_t size, std::size_t maxLongSide = 4096,
		std::size_t bufferCount = 2) {
		return Resize({size, maxLongSide, bufferCount});
	}

	[[nodiscard]] core::Result<std::unique_ptr<IPreprocessNode>> build(
		PreprocessBuildContext& context) &&;

private:
	explicit Resize(ResizeOptions options) : options_(options) {}
	ResizeOptions options_;
};

class ResizeNode final : public IPreprocessNode {
public:
	[[nodiscard]] core::Result<void> process(PreprocessContext& context) override;

private:
	friend class Resize;
	ResizeNode(ResizeOptions options, core::TensorBufferPool pool)
		: options_(options), pool_(std::move(pool)) {}

	ResizeOptions options_;
	core::TensorBufferPool pool_;
};

} // namespace visionRuntime::preprocess