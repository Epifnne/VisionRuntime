#include "cudaThresholdAlgorithm.hpp"

#include "pipeline/openCvPipelineBuilder.hpp"

#include <opencv2/imgproc.hpp>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
using namespace visionRuntime;
using namespace visionSample::openCvCuda;

int selfTest(pipeline::OpenCvPipeline<InspectionResult>& pipeline) {
	auto image = std::make_shared<cv::Mat>(32, 64, CV_8UC1, cv::Scalar(127));
	image->colRange(32, 64).setTo(128);
	cv::Mat reference;
	cv::threshold(*image, reference, 127, 255, cv::THRESH_BINARY);
	const auto expected = static_cast<double>(cv::countNonZero(reference)) /
		static_cast<double>(reference.total());
	auto buffer = core::TensorBuffer::share(image, image->data,
		image->total() * image->elemSize(), core::Device::cpu(), core::MemoryKind::Host, false);
	if (!buffer) {
		std::cerr << buffer.status().toString() << '\n';
		return 1;
	}
	auto frame = vision::Frame::create(std::move(buffer).value(), 64, 32,
		vision::PixelFormat::Gray8);
	if (!frame) {
		std::cerr << frame.status().toString() << '\n';
		return 1;
	}
	const auto result = pipeline.run(pipeline::PipelinePacket(std::move(frame).value()));
	if (!result) {
		std::cerr << result.status().toString() << '\n';
		return 1;
	}
	if (result->brightFraction != expected || !result->accepted) {
		std::cerr << "GPU result differs from CPU reference: expected=" << expected
			<< " actual=" << result->brightFraction << '\n';
		return 1;
	}
	std::cout << "CUDA self-test passed: brightFraction=" << result->brightFraction
		<< " accepted=" << result->accepted << '\n';
	return 0;
}

int runSelfTest(int device) {
	auto algorithm = CudaThresholdAlgorithm::create(device);
	if (!algorithm) {
		std::cerr << algorithm.status().toString() << '\n';
		return 1;
	}
	std::cout << "OpenCV CUDA device=" << device << " name=" << algorithm.value()->deviceName()
		<< " (no CPU fallback)\n";
	pipeline::OpenCvPipelineBuilder<InspectionResult> builder;
	auto built = builder.setAlgorithm(std::move(algorithm).value()).build();
	if (!built) {
		std::cerr << built.status().toString() << '\n';
		return 1;
	}
	return selfTest(built.value());
}
} // namespace

int visionSample::openCvCuda::runCudaSelfTest(int device) {
	try {
		return runSelfTest(device);
	} catch (const std::exception& exception) {
		std::cerr << "CUDA sample failed: " << exception.what() << '\n';
		return 1;
	}
}
