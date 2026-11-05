#pragma once

#include "pipeline/iOpenCvAlgorithm.hpp"

#include <opencv2/core/cuda.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace visionSample::openCvCuda {

struct InspectionResult {
	std::uint64_t sequence = 0;
	double brightFraction = 0;
	bool accepted = false;
};

class CudaThresholdAlgorithm final
	: public visionRuntime::pipeline::IOpenCvAlgorithm<InspectionResult> {
public:
	[[nodiscard]] static visionRuntime::core::Result<std::unique_ptr<CudaThresholdAlgorithm>>
		create(int device = 0, double threshold = 127, double minimumBrightFraction = 0.5);

	[[nodiscard]] visionRuntime::core::Result<InspectionResult> process(
		const visionRuntime::pipeline::PipelinePacket& packet) override;

	[[nodiscard]] const std::string& deviceName() const noexcept { return deviceName_; }

private:
	CudaThresholdAlgorithm(int device, double threshold, double minimumBrightFraction,
		std::string deviceName);

	int device_;
	double threshold_;
	double minimumBrightFraction_;
	std::string deviceName_;
	std::mutex mutex_;
	cv::cuda::GpuMat image_;
	cv::cuda::GpuMat mask_;
};

} // namespace visionSample::openCvCuda
