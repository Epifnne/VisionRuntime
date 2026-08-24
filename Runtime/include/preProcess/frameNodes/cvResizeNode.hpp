#pragma once

#include "preProcess/preprocessNode.hpp"

#include <cstddef>
#include <memory>

namespace visionRuntime::preprocess {

struct CvResizeOptions {
	std::size_t shortSide = 0;
	std::size_t maxLongSide = 4096;
	bool antialias = true;
};

class CvResize {
public:
	static constexpr auto inputState = PreprocessDataState::CameraFrame;
	static constexpr auto outputState = PreprocessDataState::CameraFrame;
	static constexpr bool materializes = false;

	[[nodiscard]] static CvResize shortSide(
		std::size_t size, std::size_t maxLongSide = 4096,
		bool antialias = true) {
		return CvResize({size, maxLongSide, antialias});
	}

	[[nodiscard]] core::Result<std::unique_ptr<IPreprocessNode>> build(
		PreprocessBuildContext& context) &&;

private:
	explicit CvResize(CvResizeOptions options) : options_(options) {}
	CvResizeOptions options_;
};

class CvResizeNode final : public IPreprocessNode {
public:
	explicit CvResizeNode(CvResizeOptions options) : options_(options) {}
	[[nodiscard]] core::Result<void> process(PreprocessContext& context) override;

private:
	CvResizeOptions options_;
};

} // namespace visionRuntime::preprocess