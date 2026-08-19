#include "preprocess/frameNodes/resizeNode.hpp"

#include "vision/frame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace visionRuntime::preprocess {
namespace {

struct ResizeContributor {
	std::size_t first = 0;
	std::vector<double> weights;
};

core::Status invalidArgument(std::string message) {
	return core::Status::error(core::StatusCode::InvalidArgument, std::move(message));
}

std::vector<ResizeContributor> contributors(
	std::size_t sourceSize, std::size_t targetSize) {
	const auto scale = static_cast<double>(sourceSize) / static_cast<double>(targetSize);
	const auto filterScale = std::max(1.0, scale);
	std::vector<ResizeContributor> result(targetSize);
	for (std::size_t target = 0; target < targetSize; ++target) {
		const auto center = (static_cast<double>(target) + 0.5) * scale;
		const auto first = static_cast<std::size_t>(std::max(
			0, static_cast<int>(center - filterScale + 0.5)));
		const auto last = static_cast<std::size_t>(std::min(
			static_cast<int>(sourceSize),
			static_cast<int>(center + filterScale + 0.5)));
		auto& contributor = result[target];
		contributor.first = first;
		double sum = 0.0;
		for (auto source = first; source < last; ++source) {
			const auto distance = std::abs(
				(static_cast<double>(source) + 0.5 - center) / filterScale);
			const auto weight = distance < 1.0 ? 1.0 - distance : 0.0;
			contributor.weights.push_back(weight);
			sum += weight;
		}
		for (auto& weight : contributor.weights) {
			weight /= sum;
		}
	}
	return result;
}

} // namespace

core::Result<std::unique_ptr<IPreprocessNode>> Resize::build(
	PreprocessBuildContext& context) && {
	if (options_.shortSide == 0 || options_.maxLongSide < options_.shortSide ||
		options_.bufferCount == 0 ||
		options_.shortSide > std::numeric_limits<std::size_t>::max() /
			options_.maxLongSide ||
		options_.shortSide * options_.maxLongSide >
			std::numeric_limits<std::size_t>::max() / 4) {
		return core::Result<std::unique_ptr<IPreprocessNode>>::failure(
			invalidArgument("resize dimensions and buffer count must be valid"));
	}
	auto pool = core::TensorBufferPool::create(
		options_.bufferCount, options_.shortSide * options_.maxLongSide * 4);
	if (!pool) {
		return core::Result<std::unique_ptr<IPreprocessNode>>::failure(pool.status());
	}
	context.frameSize.reset();
	std::unique_ptr<IPreprocessNode> node(new ResizeNode(
		options_, std::move(pool).value()));
	return core::Result<std::unique_ptr<IPreprocessNode>>::success(std::move(node));
}

core::Result<void> ResizeNode::process(PreprocessContext& context) {
	const auto* frame = context.currentFrame();
	if (frame == nullptr || !*frame || frame->data() == nullptr) {
		return core::Result<void>::failure(
			invalidArgument("resize requires a host-accessible frame"));
	}
	if (frame->pixelFormat() != vision::PixelFormat::Gray8 &&
		frame->pixelFormat() != vision::PixelFormat::Bgr8 &&
		frame->pixelFormat() != vision::PixelFormat::Bgra8) {
		return core::Result<void>::failure(core::Status::error(
			core::StatusCode::Unsupported,
			"resize supports Gray8, Bgr8, and Bgra8 frames"));
	}
	const auto pixelBytes = vision::pixelFormatSize(frame->pixelFormat());
	const auto sourceShortSide = std::min(frame->width(), frame->height());
	const auto scale = static_cast<double>(options_.shortSide) /
		static_cast<double>(sourceShortSide);
	const auto outputWidth = static_cast<std::size_t>(
		static_cast<double>(frame->width()) * scale);
	const auto outputHeight = static_cast<std::size_t>(
		static_cast<double>(frame->height()) * scale);
	if (std::max(outputWidth, outputHeight) > options_.maxLongSide) {
		return core::Result<void>::failure(invalidArgument(
			"resize output exceeds maxLongSide"));
	}

	auto buffer = pool_.acquire();
	if (!buffer) {
		return core::Result<void>::failure(buffer.status());
	}
	auto outputFrame = vision::Frame::create(
		std::move(buffer).value(), outputWidth, outputHeight,
		frame->pixelFormat(), outputWidth * pixelBytes, frame->metadata());
	if (!outputFrame) {
		return core::Result<void>::failure(outputFrame.status());
	}

	const auto horizontal = contributors(frame->width(), outputWidth);
	const auto vertical = contributors(frame->height(), outputHeight);
	auto* output = static_cast<std::uint8_t*>(outputFrame->data());
	const auto* input = static_cast<const std::uint8_t*>(frame->data());
	for (std::size_t y = 0; y < outputHeight; ++y) {
		for (std::size_t x = 0; x < outputWidth; ++x) {
			for (std::size_t channel = 0; channel < pixelBytes; ++channel) {
				double value = 0.0;
				for (std::size_t sourceY = 0;
					sourceY < vertical[y].weights.size(); ++sourceY) {
					for (std::size_t sourceX = 0;
						sourceX < horizontal[x].weights.size(); ++sourceX) {
						const auto offset =
							(vertical[y].first + sourceY) * frame->rowStride() +
							(horizontal[x].first + sourceX) * pixelBytes + channel;
						value += static_cast<double>(input[offset]) *
							vertical[y].weights[sourceY] * horizontal[x].weights[sourceX];
					}
				}
				output[(y * outputWidth + x) * pixelBytes + channel] =
					static_cast<std::uint8_t>(std::clamp(std::round(value), 0.0, 255.0));
			}
		}
	}

	if (context.transformContext.sourceSize.width == 0) {
		context.transformContext.sourceSize = {
			static_cast<std::uint32_t>(frame->width()),
			static_cast<std::uint32_t>(frame->height())};
	}
	context.transformContext.scaleX *= static_cast<float>(outputWidth) /
		static_cast<float>(frame->width());
	context.transformContext.scaleY *= static_cast<float>(outputHeight) /
		static_cast<float>(frame->height());
	context.setWorkingFrame(std::move(outputFrame).value());
	return core::Result<void>::success();
}

} // namespace visionRuntime::preprocess