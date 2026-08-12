#pragma once

#include "core/result.hpp"
#include "core/tensorBufferPool.hpp"
#include "preprocess/iPreprocessor.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace visonRuntime::preprocess {

struct OpenCvNchwPreprocessorOptions {
	std::string inputName = "input";
	std::size_t width = 0;
	std::size_t height = 0;
	std::size_t bufferCount = 2;
	float scale = 1.0F / 255.0F;
	std::array<float, 3> mean{0.0F, 0.0F, 0.0F};
	std::array<float, 3> standardDeviation{1.0F, 1.0F, 1.0F};
};

class OpenCvNchwPreprocessor final : public IPreprocessor {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<OpenCvNchwPreprocessor>> create(
		OpenCvNchwPreprocessorOptions options);

	[[nodiscard]] core::Result<PreparedInput> process(
		pipeline::PipelinePacket packet) override;

	[[nodiscard]] std::size_t availableBuffers() const;

private:
	OpenCvNchwPreprocessor(
		OpenCvNchwPreprocessorOptions options,
		core::TensorBufferPool pool);

	OpenCvNchwPreprocessorOptions options_;
	core::TensorBufferPool pool_;
};

} // namespace visonRuntime::preprocess