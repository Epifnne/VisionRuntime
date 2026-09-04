#pragma once

#include "memory/cpuBufferPool.hpp"
#include "preProcess/preprocessNode.hpp"

#include <cstddef>
#include <memory>
#include <optional>

namespace visionRuntime::preprocess {

struct CvResizeOptions {
	std::size_t shortSide = 0;
	std::size_t maxLongSide = 4096;
	bool antialias = true;
	std::size_t bufferCount = 2;
};

class CvResize {
public:
	static constexpr auto inputState = PreprocessDataState::CameraFrame;
	static constexpr auto outputState = PreprocessDataState::CameraFrame;
	static constexpr bool materializes = false;

	[[nodiscard]] static CvResize shortSide(
		std::size_t size, std::size_t maxLongSide = 4096,
		bool antialias = true) {
		return CvResize({size, maxLongSide, antialias, 2});
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
	std::optional<memory::CpuBufferPool> outputPool_;
};

} // namespace visionRuntime::preprocess