#include "preProcess/frameNodes/toTensorNode.hpp"

#include "core/tensor.hpp"
#include "vision/frame.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace visionRuntime::preprocess {
namespace {

core::Status invalidArgument(std::string message) {
	return core::Status::error(core::StatusCode::InvalidArgument, std::move(message));
}

std::array<float, 3> readRgb(
	const vision::Frame& frame, std::size_t x, std::size_t y) {
	const auto* row = static_cast<const std::uint8_t*>(frame.data()) +
		y * frame.rowStride();
	if (frame.pixelFormat() == vision::PixelFormat::Gray8) {
		const auto value = static_cast<float>(row[x]);
		return {value, value, value};
	}
	const auto channels = frame.pixelFormat() == vision::PixelFormat::Bgr8 ? 3U : 4U;
	const auto* pixel = row + x * channels;
	return {static_cast<float>(pixel[2]), static_cast<float>(pixel[1]),
		static_cast<float>(pixel[0])};
}

} // namespace

core::Result<std::unique_ptr<IPreprocessNode>> ToTensor::build(
	PreprocessBuildContext& context) && {
	if (options_.tensorName.empty() || options_.bufferCount == 0 ||
		(options_.channels != 1 && options_.channels != 3) ||
		!context.frameSize || context.frameSize->width == 0 ||
		context.frameSize->height == 0) {
		return core::Result<std::unique_ptr<IPreprocessNode>>::failure(invalidArgument(
			"to-tensor requires a name, buffer count, and known input frame size"));
	}
	const auto width = static_cast<std::size_t>(context.frameSize->width);
	const auto height = static_cast<std::size_t>(context.frameSize->height);
	if (width > std::numeric_limits<std::size_t>::max() / height ||
		width * height > std::numeric_limits<std::size_t>::max() /
			(options_.channels * sizeof(float))) {
		return core::Result<std::unique_ptr<IPreprocessNode>>::failure(
			invalidArgument("to-tensor input size exceeds limits"));
	}
	auto pool = core::TensorBufferPool::create(
		options_.bufferCount, width * height * options_.channels * sizeof(float));
	if (!pool) {
		return core::Result<std::unique_ptr<IPreprocessNode>>::failure(pool.status());
	}
	context.currentTensorName = options_.tensorName;
	std::unique_ptr<IPreprocessNode> node(new ToTensorNode(
		std::move(options_), *context.frameSize, std::move(pool).value()));
	return core::Result<std::unique_ptr<IPreprocessNode>>::success(std::move(node));
}

core::Result<void> ToTensorNode::process(PreprocessContext& context) {
	const auto* frame = context.currentFrame();
	if (frame == nullptr || !*frame || frame->data() == nullptr ||
		frame->width() != inputSize_.width || frame->height() != inputSize_.height) {
		return core::Result<void>::failure(
			invalidArgument("to-tensor frame does not match its configured input size"));
	}
	if (frame->pixelFormat() != vision::PixelFormat::Gray8 &&
		frame->pixelFormat() != vision::PixelFormat::Bgr8 &&
		frame->pixelFormat() != vision::PixelFormat::Bgra8) {
		return core::Result<void>::failure(
			invalidArgument("to-tensor frame pixel format is unsupported"));
	}
	if (options_.channels == 1 &&
		frame->pixelFormat() != vision::PixelFormat::Gray8) {
		return core::Result<void>::failure(
			invalidArgument("single-channel to-tensor requires a Gray8 frame"));
	}
	auto buffer = pool_.acquire();
	if (!buffer) {
		return core::Result<void>::failure(buffer.status());
	}
	auto tensor = core::Tensor::wrap(
		std::move(buffer).value(), core::DataType::Float32,
		core::TensorShape{1, static_cast<std::int64_t>(options_.channels),
			static_cast<std::int64_t>(inputSize_.height),
			static_cast<std::int64_t>(inputSize_.width)}, core::TensorLayout::Nchw);
	if (!tensor) {
		return core::Result<void>::failure(tensor.status());
	}
	auto* output = static_cast<float*>(tensor->data());
	const auto planeSize = static_cast<std::size_t>(inputSize_.width) * inputSize_.height;
	if (options_.channels == 1) {
		for (std::size_t y = 0; y < inputSize_.height; ++y) {
			const auto* input = static_cast<const std::uint8_t*>(frame->data()) +
				y * frame->rowStride();
			for (std::size_t x = 0; x < inputSize_.width; ++x) {
				output[y * inputSize_.width + x] = static_cast<float>(input[x]);
			}
		}
	} else {
		for (std::size_t y = 0; y < inputSize_.height; ++y) {
			for (std::size_t x = 0; x < inputSize_.width; ++x) {
				const auto rgb = readRgb(*frame, x, y);
				const auto offset = y * inputSize_.width + x;
				for (std::size_t channel = 0; channel < options_.channels; ++channel) {
					output[channel * planeSize + offset] = rgb[channel];
				}
			}
		}
	}
	if (context.transformContext.sourceSize.width == 0) {
		context.transformContext.sourceSize = inputSize_;
	}
	context.transformContext.networkSize = inputSize_;
	context.tensors.emplace(options_.tensorName, std::move(tensor).value());
	context.workingFrame.reset();
	context.packet.completeImagePreparation();
	return core::Result<void>::success();
}

} // namespace visionRuntime::preprocess