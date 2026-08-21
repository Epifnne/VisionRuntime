#include "preProcess/fusedImageToTensorNode.hpp"
#include "preProcess/tensorNormalizeNode.hpp"

#include "core/tensor.hpp"
#include "vision/frame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace visonRuntime::preprocess {
namespace {

[[nodiscard]] core::Status error(core::StatusCode code, std::string message) {
	return core::Status::error(code, std::move(message));
}

[[nodiscard]] std::array<float, 3> readRgb(
	const vision::Frame& frame, std::size_t x, std::size_t y) {
	const auto* row = static_cast<const std::uint8_t*>(frame.data()) + y * frame.rowStride();
	if (frame.pixelFormat() == vision::PixelFormat::Gray8) {
		const auto value = static_cast<float>(row[x]);
		return {value, value, value};
	}
	const auto channelCount = frame.pixelFormat() == vision::PixelFormat::Bgr8 ? 3U : 4U;
	const auto* pixel = row + x * channelCount;
	return {static_cast<float>(pixel[2]), static_cast<float>(pixel[1]),
		static_cast<float>(pixel[0])};
}

[[nodiscard]] std::array<float, 3> sample(
	const vision::Frame& frame,
	std::size_t outputX,
	std::size_t outputY,
	std::size_t outputWidth,
	std::size_t outputHeight) {
	const auto sourceX = std::max(0.0F,
		(static_cast<float>(outputX) + 0.5F) * static_cast<float>(frame.width()) /
			static_cast<float>(outputWidth) - 0.5F);
	const auto sourceY = std::max(0.0F,
		(static_cast<float>(outputY) + 0.5F) * static_cast<float>(frame.height()) /
			static_cast<float>(outputHeight) - 0.5F);
	const auto left = std::min(static_cast<std::size_t>(sourceX), frame.width() - 1);
	const auto top = std::min(static_cast<std::size_t>(sourceY), frame.height() - 1);
	const auto right = std::min(left + 1, frame.width() - 1);
	const auto bottom = std::min(top + 1, frame.height() - 1);
	const auto xWeight = sourceX - static_cast<float>(left);
	const auto yWeight = sourceY - static_cast<float>(top);
	const auto topLeft = readRgb(frame, left, top);
	const auto topRight = readRgb(frame, right, top);
	const auto bottomLeft = readRgb(frame, left, bottom);
	const auto bottomRight = readRgb(frame, right, bottom);
	std::array<float, 3> result;
	for (std::size_t channel = 0; channel < result.size(); ++channel) {
		const auto topValue = topLeft[channel] +
			(topRight[channel] - topLeft[channel]) * xWeight;
		const auto bottomValue = bottomLeft[channel] +
			(bottomRight[channel] - bottomLeft[channel]) * xWeight;
		result[channel] = topValue + (bottomValue - topValue) * yWeight;
	}
	return result;
}

} // namespace

core::Result<FusedImageToTensorNode> FusedImageToTensorNode::create(
	FusedImageToTensorOptions options) {
	if (options.inputName.empty() || options.width == 0 || options.height == 0 ||
		options.bufferCount == 0) {
		return core::Result<FusedImageToTensorNode>::failure(error(
			core::StatusCode::InvalidArgument,
			"image-to-tensor name, dimensions, and buffer count must be valid"));
	}
	if (options.width > std::numeric_limits<std::uint32_t>::max() ||
		options.height > std::numeric_limits<std::uint32_t>::max() ||
		options.width > std::numeric_limits<std::size_t>::max() / options.height ||
		options.width * options.height >
			std::numeric_limits<std::size_t>::max() / (3 * sizeof(float))) {
		return core::Result<FusedImageToTensorNode>::failure(error(
			core::StatusCode::InvalidArgument, "image-to-tensor size exceeds limits"));
	}
	auto pool = core::TensorBufferPool::create(
		options.bufferCount, options.width * options.height * 3 * sizeof(float));
	if (!pool) {
		return core::Result<FusedImageToTensorNode>::failure(pool.status());
	}
	return core::Result<FusedImageToTensorNode>::success(FusedImageToTensorNode(
		std::move(options), std::move(pool).value()));
}

FusedImageToTensorNode::FusedImageToTensorNode(
	FusedImageToTensorOptions options, core::TensorBufferPool pool)
	: options_(std::move(options)), pool_(std::move(pool)) {}

core::Result<void> FusedImageToTensorNode::process(PreprocessContext& context) {
	const auto* frame = context.packet.cameraFrame();
	if (frame == nullptr || !*frame || frame->data() == nullptr) {
		return core::Result<void>::failure(error(
			core::StatusCode::InvalidArgument,
			"image-to-tensor requires a host-accessible camera frame"));
	}
	if (frame->pixelFormat() != vision::PixelFormat::Gray8 &&
		frame->pixelFormat() != vision::PixelFormat::Bgr8 &&
		frame->pixelFormat() != vision::PixelFormat::Bgra8) {
		return core::Result<void>::failure(error(
			core::StatusCode::Unsupported,
			"image-to-tensor supports Gray8, Bgr8, and Bgra8"));
	}

	auto buffer = pool_.acquire();
	if (!buffer) {
		return core::Result<void>::failure(buffer.status());
	}
	auto tensor = core::Tensor::wrap(
		std::move(buffer).value(), core::DataType::Float32,
		core::TensorShape{1, 3, static_cast<std::int64_t>(options_.height),
			static_cast<std::int64_t>(options_.width)}, core::TensorLayout::Nchw);
	if (!tensor) {
		return core::Result<void>::failure(tensor.status());
	}

	auto* output = static_cast<float*>(tensor->data());
	const auto planeSize = options_.width * options_.height;
	for (std::size_t y = 0; y < options_.height; ++y) {
		for (std::size_t x = 0; x < options_.width; ++x) {
			const auto rgb = sample(*frame, x, y, options_.width, options_.height);
			const auto pixelIndex = y * options_.width + x;
			for (std::size_t channel = 0; channel < rgb.size(); ++channel) {
				output[channel * planeSize + pixelIndex] = rgb[channel];
			}
		}
	}

	context.transformContext.sourceSize = {
		static_cast<std::uint32_t>(frame->width()),
		static_cast<std::uint32_t>(frame->height())};
	context.transformContext.networkSize = {
		static_cast<std::uint32_t>(options_.width),
		static_cast<std::uint32_t>(options_.height)};
	context.transformContext.scaleX = static_cast<float>(options_.width) /
		static_cast<float>(frame->width());
	context.transformContext.scaleY = static_cast<float>(options_.height) /
		static_cast<float>(frame->height());
	context.tensors.emplace(options_.inputName, std::move(tensor).value());
	context.packet.completeImagePreparation();
	return core::Result<void>::success();
}

std::size_t FusedImageToTensorNode::availableBuffers() const {
	return pool_.available();
}

core::Result<TensorNormalizeNode> TensorNormalizeNode::create(
	TensorNormalizeOptions options) {
	if (options.inputName.empty() || !std::isfinite(options.scale)) {
		return core::Result<TensorNormalizeNode>::failure(error(
			core::StatusCode::InvalidArgument, "normalize name and scale must be valid"));
	}
	for (std::size_t channel = 0; channel < 3; ++channel) {
		if (!std::isfinite(options.mean[channel]) ||
			!std::isfinite(options.standardDeviation[channel]) ||
			options.standardDeviation[channel] == 0.0F) {
			return core::Result<TensorNormalizeNode>::failure(error(
				core::StatusCode::InvalidArgument,
				"normalize values must be finite and deviation non-zero"));
		}
	}
	return core::Result<TensorNormalizeNode>::success(
		TensorNormalizeNode(std::move(options)));
}

TensorNormalizeNode::TensorNormalizeNode(TensorNormalizeOptions options)
	: options_(std::move(options)) {}

core::Result<void> TensorNormalizeNode::process(PreprocessContext& context) {
	auto iterator = context.tensors.find(options_.inputName);
	if (iterator == context.tensors.end()) {
		return core::Result<void>::failure(error(
			core::StatusCode::InvalidState, "normalize input tensor does not exist"));
	}
	auto& tensor = iterator->second;
	const auto& dimensions = tensor.shape().dimensions();
	if (tensor.dataType() != core::DataType::Float32 ||
		tensor.layout() != core::TensorLayout::Nchw || dimensions.size() != 4 ||
		dimensions[0] != 1 || dimensions[1] != 3 || !tensor.isContiguous() ||
		tensor.data() == nullptr) {
		return core::Result<void>::failure(error(
			core::StatusCode::Unsupported,
			"normalize requires writable contiguous Float32 NCHW with three channels"));
	}

	const auto planeSize = static_cast<std::size_t>(dimensions[2]) *
		static_cast<std::size_t>(dimensions[3]);
	auto* values = static_cast<float*>(tensor.data());
	for (std::size_t channel = 0; channel < 3; ++channel) {
		for (std::size_t index = 0; index < planeSize; ++index) {
			auto& value = values[channel * planeSize + index];
			value = (value * options_.scale - options_.mean[channel]) /
				options_.standardDeviation[channel];
		}
	}
	return core::Result<void>::success();
}

} // namespace visonRuntime::preprocess
