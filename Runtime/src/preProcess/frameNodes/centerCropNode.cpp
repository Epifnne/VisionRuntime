#include "preprocess/frameNodes/centerCropNode.hpp"

#include "vision/frame.hpp"

#include <cstdint>

namespace visionRuntime::preprocess {
namespace {

core::Status invalidArgument(std::string message) {
	return core::Status::error(core::StatusCode::InvalidArgument, std::move(message));
}

} // namespace

core::Result<std::unique_ptr<IPreprocessNode>> CenterCrop::build(
	PreprocessBuildContext& context) && {
	if (size_.width == 0 || size_.height == 0) {
		return core::Result<std::unique_ptr<IPreprocessNode>>::failure(
			invalidArgument("center crop dimensions must be greater than zero"));
	}
	context.frameSize = vision::ImageSize{
		static_cast<std::uint32_t>(size_.width),
		static_cast<std::uint32_t>(size_.height)};
	std::unique_ptr<IPreprocessNode> node =
		std::make_unique<CenterCropNode>(size_);
	return core::Result<std::unique_ptr<IPreprocessNode>>::success(std::move(node));
}

core::Result<void> CenterCropNode::process(PreprocessContext& context) {
	const auto* frame = context.currentFrame();
	if (frame == nullptr || !*frame || size_.width > frame->width() ||
		size_.height > frame->height()) {
		return core::Result<void>::failure(
			invalidArgument("center crop exceeds the current frame"));
	}
	const auto left = (frame->width() - size_.width) / 2;
	const auto top = (frame->height() - size_.height) / 2;
	const auto byteOffset = frame->byteOffset() + top * frame->rowStride() +
		left * vision::pixelFormatSize(frame->pixelFormat());
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