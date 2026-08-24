#include "preProcess/tensorNodes/normalizeNode.hpp"

#include <cmath>
#include <utility>

namespace visionRuntime::preprocess {
namespace {

[[nodiscard]] core::Status invalidArgument(std::string message) {
	return core::Status::error(core::StatusCode::InvalidArgument, std::move(message));
}

} // namespace

core::Result<std::unique_ptr<IPreprocessNode>> Normalize::build(
	PreprocessBuildContext& context) && {
	if (context.currentTensorName.empty() || !std::isfinite(options_.scale) ||
		options_.mean.empty() ||
		options_.mean.size() != options_.standardDeviation.size()) {
		return core::Result<std::unique_ptr<IPreprocessNode>>::failure(
			invalidArgument("normalize name and scale must be valid"));
	}
	for (std::size_t channel = 0; channel < options_.mean.size(); ++channel) {
		if (!std::isfinite(options_.mean[channel]) ||
			!std::isfinite(options_.standardDeviation[channel]) ||
			options_.standardDeviation[channel] == 0.0F) {
			return core::Result<std::unique_ptr<IPreprocessNode>>::failure(
				invalidArgument(
					"normalize values must be finite and deviation non-zero"));
		}
	}
	std::unique_ptr<IPreprocessNode> node(new NormalizeNode(
		context.currentTensorName, std::move(options_)));
	return core::Result<std::unique_ptr<IPreprocessNode>>::success(std::move(node));
}

core::Result<void> NormalizeNode::process(PreprocessContext& context) {
	auto iterator = context.tensors.find(inputName_);
	if (iterator == context.tensors.end()) {
		return core::Result<void>::failure(core::Status::error(
			core::StatusCode::InvalidState,
			"normalize input tensor does not exist"));
	}
	auto& tensor = iterator->second;
	const auto& dimensions = tensor.shape().dimensions();
	if (tensor.dataType() != core::DataType::Float32 ||
		tensor.layout() != core::TensorLayout::Nchw || dimensions.size() != 4 ||
		dimensions[0] != 1 || dimensions[1] <= 0 ||
		static_cast<std::size_t>(dimensions[1]) != options_.mean.size() ||
		!tensor.isContiguous() || tensor.data() == nullptr) {
		return core::Result<void>::failure(core::Status::error(
			core::StatusCode::Unsupported,
			"normalize parameters must match writable contiguous Float32 NCHW channels"));
	}

	const auto planeSize = static_cast<std::size_t>(dimensions[2] * dimensions[3]);
	auto* values = static_cast<float*>(tensor.data());
	for (std::size_t channel = 0; channel < options_.mean.size(); ++channel) {
		for (std::size_t index = 0; index < planeSize; ++index) {
			auto& value = values[channel * planeSize + index];
			value = (value * options_.scale - options_.mean[channel]) /
				options_.standardDeviation[channel];
		}
	}
	return core::Result<void>::success();
}

} // namespace visionRuntime::preprocess