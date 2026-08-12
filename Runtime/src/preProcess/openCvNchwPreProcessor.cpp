#include "preprocess/openCvNchwPreprocessor.hpp"

#include "core/tensor.hpp"
#include "vision/frame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace visonRuntime::preprocess {
namespace {

[[nodiscard]] core::Status invalidArgument(std::string message) {
	return core::Status::error(core::StatusCode::InvalidArgument, std::move(message));
}

[[nodiscard]] core::Status unsupported(std::string message) {
	return core::Status::error(core::StatusCode::Unsupported, std::move(message));
}

[[nodiscard]] std::array<float, 3> readRgb(
	const vision::Frame& frame,
	std::size_t x,
	std::size_t y) {
	const auto* row = static_cast<const std::uint8_t*>(frame.data()) + y * frame.rowStride();
	switch (frame.pixelFormat()) {
	case vision::PixelFormat::Gray8: {
		const auto value = static_cast<float>(row[x]);
		return {value, value, value};
	}
	case vision::PixelFormat::Bgr8: {
		const auto* pixel = row + x * 3;
		return {static_cast<float>(pixel[2]), static_cast<float>(pixel[1]),
			static_cast<float>(pixel[0])};
	}
	case vision::PixelFormat::Bgra8: {
		const auto* pixel = row + x * 4;
		return {static_cast<float>(pixel[2]), static_cast<float>(pixel[1]),
			static_cast<float>(pixel[0])};
	}
	default:
		return {};
	}
}

[[nodiscard]] std::array<float, 3> bilinearSample(
	const vision::Frame& frame,
	std::size_t outputX,
	std::size_t outputY,
	std::size_t outputWidth,
	std::size_t outputHeight) {
	const auto sourceX = std::max(0.0F,
		(static_cast<float>(outputX) + 0.5F) *
			static_cast<float>(frame.width()) / static_cast<float>(outputWidth) - 0.5F);
	const auto sourceY = std::max(0.0F,
		(static_cast<float>(outputY) + 0.5F) *
			static_cast<float>(frame.height()) / static_cast<float>(outputHeight) - 0.5F);
	const auto left = std::min(
		static_cast<std::size_t>(sourceX), frame.width() - 1);
	const auto top = std::min(
		static_cast<std::size_t>(sourceY), frame.height() - 1);
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

core::Result<std::unique_ptr<OpenCvNchwPreprocessor>>
OpenCvNchwPreprocessor::create(OpenCvNchwPreprocessorOptions options) {
	if (options.inputName.empty()) {
		return core::Result<std::unique_ptr<OpenCvNchwPreprocessor>>::failure(
			invalidArgument("preprocessor input name must not be empty"));
	}
	if (options.width == 0 || options.height == 0 || options.bufferCount == 0) {
		return core::Result<std::unique_ptr<OpenCvNchwPreprocessor>>::failure(
			invalidArgument("preprocessor dimensions and buffer count must be greater than zero"));
	}
	for (const auto deviation : options.standardDeviation) {
		if (deviation == 0.0F || !std::isfinite(deviation)) {
			return core::Result<std::unique_ptr<OpenCvNchwPreprocessor>>::failure(
				invalidArgument("preprocessor standard deviation must be finite and non-zero"));
		}
	}
	if (!std::isfinite(options.scale)) {
		return core::Result<std::unique_ptr<OpenCvNchwPreprocessor>>::failure(
			invalidArgument("preprocessor scale must be finite"));
	}
	if (options.width > std::numeric_limits<std::size_t>::max() / options.height ||
		options.width * options.height >
			std::numeric_limits<std::size_t>::max() / (3 * sizeof(float))) {
		return core::Result<std::unique_ptr<OpenCvNchwPreprocessor>>::failure(
			invalidArgument("preprocessor tensor size overflows size_t"));
	}

	const auto byteSize = options.width * options.height * 3 * sizeof(float);
	auto pool = core::TensorBufferPool::create(options.bufferCount, byteSize);
	if (!pool) {
		return core::Result<std::unique_ptr<OpenCvNchwPreprocessor>>::failure(pool.status());
	}
	return core::Result<std::unique_ptr<OpenCvNchwPreprocessor>>::success(
		std::unique_ptr<OpenCvNchwPreprocessor>(new OpenCvNchwPreprocessor(
			std::move(options), std::move(pool).value())));
}

OpenCvNchwPreprocessor::OpenCvNchwPreprocessor(
	OpenCvNchwPreprocessorOptions options,
	core::TensorBufferPool pool)
	: options_(std::move(options)), pool_(std::move(pool)) {}

core::Result<PreparedInput> OpenCvNchwPreprocessor::process(
	pipeline::PipelinePacket packet) {
	const auto* frame = packet.cameraFrame();
	if (frame == nullptr || !*frame) {
		return core::Result<PreparedInput>::failure(
			invalidArgument("preprocessor requires a camera frame"));
	}
	if (frame->data() == nullptr) {
		return core::Result<PreparedInput>::failure(
			unsupported("preprocessor requires host-accessible frame memory"));
	}
	if (frame->pixelFormat() != vision::PixelFormat::Gray8 &&
		frame->pixelFormat() != vision::PixelFormat::Bgr8 &&
		frame->pixelFormat() != vision::PixelFormat::Bgra8) {
		return core::Result<PreparedInput>::failure(
			unsupported("preprocessor supports Gray8, Bgr8, and Bgra8 frames"));
	}

	auto buffer = pool_.acquire();
	if (!buffer) {
		return core::Result<PreparedInput>::failure(buffer.status());
	}
	auto tensor = core::Tensor::wrap(
		std::move(buffer).value(), core::DataType::Float32,
		core::TensorShape{1, 3, static_cast<std::int64_t>(options_.height),
			static_cast<std::int64_t>(options_.width)},
		core::TensorLayout::Nchw);
	if (!tensor) {
		return core::Result<PreparedInput>::failure(tensor.status());
	}

	auto* output = static_cast<float*>(tensor->data());
	const auto planeSize = options_.width * options_.height;
	for (std::size_t y = 0; y < options_.height; ++y) {
		for (std::size_t x = 0; x < options_.width; ++x) {
			const auto rgb = bilinearSample(
				*frame, x, y, options_.width, options_.height);
			const auto pixelIndex = y * options_.width + x;
			for (std::size_t channel = 0; channel < rgb.size(); ++channel) {
				output[channel * planeSize + pixelIndex] =
					(rgb[channel] * options_.scale - options_.mean[channel]) /
					options_.standardDeviation[channel];
			}
		}
	}

	vision::TransformContext transformContext;
	transformContext.sourceSize = {
		static_cast<std::uint32_t>(frame->width()),
		static_cast<std::uint32_t>(frame->height())};
	transformContext.networkSize = {
		static_cast<std::uint32_t>(options_.width),
		static_cast<std::uint32_t>(options_.height)};
	transformContext.scaleX = static_cast<float>(options_.width) /
		static_cast<float>(frame->width());
	transformContext.scaleY = static_cast<float>(options_.height) /
		static_cast<float>(frame->height());

	packet.completeImagePreparation();
	TensorMap tensors;
	tensors.emplace(options_.inputName, std::move(tensor).value());
	return core::Result<PreparedInput>::success(PreparedInput(
		std::move(packet), std::move(tensors), transformContext));
}

std::size_t OpenCvNchwPreprocessor::availableBuffers() const {
	return pool_.available();
}

} // namespace visonRuntime::preprocess