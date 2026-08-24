#include "preProcess/frameNodes/cvResizeNode.hpp"

#include "core/tensorBuffer.hpp"
#include "vision/frame.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace visionRuntime::preprocess {
namespace {

[[nodiscard]] core::Status invalidArgument(std::string message) {
	return core::Status::error(core::StatusCode::InvalidArgument, std::move(message));
}

[[nodiscard]] int channelCount(vision::PixelFormat format) {
	switch (format) {
	case vision::PixelFormat::Gray8:
		return 1;
	case vision::PixelFormat::Bgr8:
		return 3;
	case vision::PixelFormat::Bgra8:
		return 4;
	default:
		return -1;
	}
}

[[nodiscard]] double antialiasSigma(double sourceSize, double targetSize) {
	const auto downsampleScale = sourceSize / targetSize;
	return downsampleScale > 1.0
		? std::sqrt((downsampleScale * downsampleScale - 1.0) / 6.0)
		: 0.0;
}

} // namespace

core::Result<std::unique_ptr<IPreprocessNode>> CvResize::build(
	PreprocessBuildContext& context) && {
	if (options_.shortSide == 0 || options_.maxLongSide < options_.shortSide) {
		return core::Result<std::unique_ptr<IPreprocessNode>>::failure(
			invalidArgument("cv resize dimensions must be valid"));
	}
	context.frameSize.reset();
	std::unique_ptr<IPreprocessNode> node =
		std::make_unique<CvResizeNode>(options_);
	return core::Result<std::unique_ptr<IPreprocessNode>>::success(std::move(node));
}

core::Result<void> CvResizeNode::process(PreprocessContext& context) {
	const auto* frame = context.currentFrame();
	if (frame == nullptr || !*frame || frame->data() == nullptr) {
		return core::Result<void>::failure(
			invalidArgument("cv resize requires a host-accessible frame"));
	}
	const auto channels = channelCount(frame->pixelFormat());
	if (channels < 0) {
		return core::Result<void>::failure(core::Status::error(
			core::StatusCode::Unsupported,
			"cv resize supports Gray8, Bgr8, and Bgra8 frames"));
	}
	const auto type = CV_MAKETYPE(CV_8U, channels);

	const auto sourceShortSide = std::min(frame->width(), frame->height());
	const auto outputWidth = frame->width() <= frame->height()
		? options_.shortSide
		: options_.shortSide * frame->width() / sourceShortSide;
	const auto outputHeight = frame->height() <= frame->width()
		? options_.shortSide
		: options_.shortSide * frame->height() / sourceShortSide;
	if (std::max(outputWidth, outputHeight) > options_.maxLongSide ||
		outputWidth > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
		outputHeight > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
		return core::Result<void>::failure(
			invalidArgument("cv resize output exceeds maxLongSide"));
	}

	const cv::Mat source(
		static_cast<int>(frame->height()), static_cast<int>(frame->width()), type,
		const_cast<void*>(frame->data()), frame->rowStride());
	cv::Mat filtered = source;
	if (options_.antialias) {
		const auto sigmaX = antialiasSigma(
			static_cast<double>(frame->width()), static_cast<double>(outputWidth));
		const auto sigmaY = antialiasSigma(
			static_cast<double>(frame->height()), static_cast<double>(outputHeight));
		if (sigmaX > 0.0 || sigmaY > 0.0) {
			cv::GaussianBlur(source, filtered, {}, sigmaX, sigmaY,
				cv::BORDER_REPLICATE);
		}
	}
	auto resized = std::make_shared<cv::Mat>();
	cv::resize(filtered, *resized,
		{static_cast<int>(outputWidth), static_cast<int>(outputHeight)},
		0.0, 0.0, cv::INTER_LINEAR);
	const auto rowStride = resized->step[0];
	const auto byteSize = (outputHeight - 1) * rowStride +
		outputWidth * resized->elemSize();
	std::shared_ptr<void> owner = resized;
	auto buffer = core::TensorBuffer::share(
		std::move(owner), resized->data, byteSize);
	if (!buffer) {
		return core::Result<void>::failure(buffer.status());
	}
	auto outputFrame = vision::Frame::create(
		std::move(buffer).value(), outputWidth, outputHeight,
		frame->pixelFormat(), rowStride, frame->metadata());
	if (!outputFrame) {
		return core::Result<void>::failure(outputFrame.status());
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