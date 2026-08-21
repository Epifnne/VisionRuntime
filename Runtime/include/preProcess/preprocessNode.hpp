#pragma once

#include "core/result.hpp"
#include "preProcess/preprocessContext.hpp"
#include "vision/transformContext.hpp"

#include <concepts>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace visionRuntime::preprocess {

struct PreprocessBuildContext {
	std::string currentTensorName;
	std::optional<vision::ImageSize> frameSize;
};

template<typename Node>
concept PreprocessNode = requires(Node node, PreprocessBuildContext& context) {
	{ Node::inputState } -> std::convertible_to<PreprocessDataState>;
	{ Node::outputState } -> std::convertible_to<PreprocessDataState>;
	{ Node::materializes } -> std::convertible_to<bool>;
	{ std::move(node).build(context) } ->
		std::same_as<core::Result<std::unique_ptr<IPreprocessNode>>>;
};

} // namespace visionRuntime::preprocess