#pragma once

#include "preprocess/preprocessNode.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace visionRuntime::preprocess {

struct NormalizeOptions {
	float scale = 1.0F / 255.0F;
	std::vector<float> mean{0.0F, 0.0F, 0.0F};
	std::vector<float> standardDeviation{1.0F, 1.0F, 1.0F};
};

class Normalize {
public:
	static constexpr auto inputState = PreprocessDataState::Tensor;
	static constexpr auto outputState = PreprocessDataState::Tensor;
	static constexpr bool materializes = false;

	explicit Normalize(NormalizeOptions options = {})
		: options_(std::move(options)) {}

	[[nodiscard]] core::Result<std::unique_ptr<IPreprocessNode>> build(
		PreprocessBuildContext& context) &&;

private:
	NormalizeOptions options_;
};

class NormalizeNode final : public IPreprocessNode {
public:
	[[nodiscard]] core::Result<void> process(PreprocessContext& context) override;

private:
	friend class Normalize;
	NormalizeNode(std::string inputName, NormalizeOptions options)
		: inputName_(std::move(inputName)), options_(std::move(options)) {}

	std::string inputName_;
	NormalizeOptions options_;
};

} // namespace visionRuntime::preprocess