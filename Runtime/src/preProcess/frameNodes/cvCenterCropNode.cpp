#include "preProcess/frameNodes/cvCenterCropNode.hpp"

#include "vision/frame.hpp"

#include <cmath>
#include <cstdint>
#include <utility>

namespace visionRuntime::preprocess {
namespace {

[[nodiscard]] core::Status invalidArgument(std::string message) {
	return core::Status::error(core::StatusCode::InvalidArgument, std::move(message));
}

[[nodiscard]] std::size_t channelCount(vision::PixelFormat format) {
	switch (format) {
	case vision::PixelFormat::Gray8:
		return 1;
	case vision::PixelFormat::Bgr8:
		return 3;
	case vision::PixelFormat::Bgra8:
		return 4;
	default:
		return 0;
	}
}

[[nodiscard]] std::size_t roundHalfToEven(double value) {
	const auto lower = std::floor(value);
	const auto fraction = value - lower;
	if (fraction < 0.5) {
		return static_cast<std::size_t>(lower);
	}
	if (fraction > 0.5) {
		return static_cast<std::size_t>(lower + 1.0);
	}
	const auto integer = static_cast<std::size_t>(lower);
	return integer % 2 == 0 ? integer : integer + 1;
}

} // namespace

core::Result<std::unique_ptr<IPreprocessNode>> CvCenterCrop::build(
	PreprocessBuildContext& context) && {
	if (size_.width == 0 || size_.height == 0) {
		return core::Result<std::unique_ptr<IPreprocessNode>>::failure(
			invalidArgument("cv center crop dimensions must be greater than zero"));
	}
	context.frameSize = vision::ImageSize{
		static_cast<std::uint32_t>(size_.width),
		static_cast<std::uint32_t>(size_.height)};
	std::unique_ptr<IPreprocessNode> node =
		std::make_unique<CvCenterCropNode>(size_);
	return core::Result<std::unique_ptr<IPreprocessNode>>::success(std::move(node));
}

core::Result<void> CvCenterCropNode::process(PreprocessContext& context) {
	const auto* frame = context.currentFrame();
	if (frame == nullptr || !*frame || size_.width > frame->width() ||
		size_.height > frame->height()) {
		return core::Result<void>::failure(
			invalidArgument("cv center crop exceeds the current frame"));
	}
	const auto channels = channelCount(frame->pixelFormat());
	if (channels == 0) {
		return core::Result<void>::failure(core::Status::error(
			core::StatusCode::Unsupported,
			"cv center crop supports Gray8, Bgr8, and Bgra8 frames"));
	}
	const auto left = roundHalfToEven(
		static_cast<double>(frame->width() - size_.width) / 2.0);
	const auto top = roundHalfToEven(
		static_cast<double>(frame->height() - size_.height) / 2.0);
	const auto byteOffset = frame->byteOffset() + top * frame->rowStride() +
		left * channels;
	auto cropped = vision::Frame::create(
		frame->buffer(), size_.width, size_.height, frame->pixelFormat(),
		frame->rowStride(), frame->metadata(), byteOffset);
	if (!cropped) {
		return core::Result<void>::failure(cropped.status());
	}
	if (context.transformContext.sourceSize.width == 0) {
		context.transformContext.sourceSize = {
			static_cast<std::uint32_t>(frame->width()),
			static_cast<std::uint32_t>(frame->height())};
	}
	context.transformContext.crop = {
		static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(top),
		static_cast<std::uint32_t>(size_.width),
		static_cast<std::uint32_t>(size_.height)};
	context.setWorkingFrame(std::move(cropped).value());
	return core::Result<void>::success();
}

} // namespace visionRuntime::preprocess