#pragma once

#include "preprocess/preprocessContext.hpp"

#include <array>
#include <string>

namespace visonRuntime::preprocess {

struct TensorNormalizeOptions {
	std::string inputName = "input";
	float scale = 1.0F / 255.0F;
	std::array<float, 3> mean{0.0F, 0.0F, 0.0F};
	std::array<float, 3> standardDeviation{1.0F, 1.0F, 1.0F};
};

class TensorNormalizeNode final : public IPreprocessNode {
public:
	static constexpr auto inputState = PreprocessDataState::Tensor;
	static constexpr auto outputState = PreprocessDataState::Tensor;
	static constexpr bool materializes = false;

	[[nodiscard]] static core::Result<TensorNormalizeNode> create(
		TensorNormalizeOptions options);
	[[nodiscard]] core::Result<void> process(PreprocessContext& context) override;

private:
	explicit TensorNormalizeNode(TensorNormalizeOptions options);
	TensorNormalizeOptions options_;
};

} // namespace visonRuntime::preprocess
