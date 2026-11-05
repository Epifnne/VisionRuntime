#include "cudaThresholdAlgorithm.hpp"

#include <opencv2/cudaarithm.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <limits>
#include <utility>

namespace visionSample::openCvCuda {
using namespace visionRuntime;

CudaThresholdAlgorithm::CudaThresholdAlgorithm(int device, double threshold,
	double minimumBrightFraction, std::string deviceName)
	: device_(device), threshold_(threshold), minimumBrightFraction_(minimumBrightFraction),
	  deviceName_(std::move(deviceName)) {}

core::Result<std::unique_ptr<CudaThresholdAlgorithm>> CudaThresholdAlgorithm::create(
	int device, double threshold, double minimumBrightFraction) {
	using Result = core::Result<std::unique_ptr<CudaThresholdAlgorithm>>;
	if (device < 0 || !std::isfinite(threshold) || threshold < 0 || threshold > 255 ||
		!std::isfinite(minimumBrightFraction) ||
		minimumBrightFraction < 0 || minimumBrightFraction > 1) {
		return Result::failure(core::Status::error(core::StatusCode::InvalidArgument,
			"device must be nonnegative, threshold in [0,255], and minimum fraction in [0,1]"));
	}
	try {
		const auto count = cv::cuda::getCudaEnabledDeviceCount();
		if (count <= 0) {
			return Result::failure(core::Status::error(core::StatusCode::Unavailable,
				"OpenCV CUDA has no usable NVIDIA GPU/driver; CPU fallback is disabled"));
		}
		if (device >= count) {
			return Result::failure(core::Status::error(core::StatusCode::InvalidArgument,
				"CUDA device index is out of range; available devices: " + std::to_string(count)));
		}
		const cv::cuda::DeviceInfo info(device);
		if (!info.isCompatible()) {
			return Result::failure(core::Status::error(core::StatusCode::Unsupported,
				"GPU is not compatible with this OpenCV CUDA build; rebuild for its compute capability"));
		}
		cv::cuda::setDevice(device);
		return Result::success(std::unique_ptr<CudaThresholdAlgorithm>(
			new CudaThresholdAlgorithm(device, threshold, minimumBrightFraction, info.name())));
	} catch (const cv::Exception& exception) {
		return Result::failure(core::Status::error(core::StatusCode::BackendError,
			std::string("OpenCV CUDA initialization failed: ") + exception.what()));
	}
}

core::Result<InspectionResult> CudaThresholdAlgorithm::process(
	const pipeline::PipelinePacket& packet) {
	using Result = core::Result<InspectionResult>;
	const auto* frame = packet.cameraFrame();
	if (!frame || frame->empty() || !frame->buffer().isHostAccessible()) {
		return Result::failure(core::Status::error(core::StatusCode::InvalidArgument,
			"CUDA threshold requires a nonempty host-accessible camera frame"));
	}
	if (frame->pixelFormat() != vision::PixelFormat::Gray8) {
		return Result::failure(core::Status::error(core::StatusCode::Unsupported,
			"CUDA threshold requires Gray8 input"));
	}
	const auto maxPixels = static_cast<std::size_t>(std::numeric_limits<int>::max());
	if (frame->width() > maxPixels || frame->height() > maxPixels / frame->width()) {
		return Result::failure(core::Status::error(core::StatusCode::InvalidArgument,
			"CUDA countNonZero requires the pixel count to fit in int"));
	}
	std::lock_guard lock(mutex_);
	try {
		// Executors can invoke the same algorithm on different host threads.
		cv::cuda::setDevice(device_);
		const cv::Mat host(static_cast<int>(frame->height()), static_cast<int>(frame->width()),
			CV_8UC1, const_cast<void*>(frame->data()), frame->rowStride());
		// Blocking operations keep borrowed frame storage valid on success and failure.
		image_.upload(host);
		cv::cuda::threshold(image_, mask_, threshold_, 255, cv::THRESH_BINARY);
		const auto brightPixels = cv::cuda::countNonZero(mask_);
		const auto fraction = static_cast<double>(brightPixels) /
			static_cast<double>(frame->width() * frame->height());
		return Result::success({frame->metadata().sequenceNumber, fraction,
			fraction >= minimumBrightFraction_});
	} catch (const cv::Exception& exception) {
		return Result::failure(core::Status::error(core::StatusCode::BackendError,
			std::string("OpenCV CUDA threshold failed: ") + exception.what()));
	}
}

} // namespace visionSample::openCvCuda
