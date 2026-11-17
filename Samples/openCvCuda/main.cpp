#include "cudaThresholdAlgorithm.hpp"

#include "camera/fileSource.hpp"
#include "pipeline/openCvPipelineBuilder.hpp"
#include "runtime/runtimeFactory.hpp"
#include "exports/productEntry.hpp"
#include "../common/profileCli.hpp"

#include <opencv2/imgproc.hpp>

#include <charconv>
#include <filesystem>
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

int main(int argc, char* argv[]) {
	if (argc == 2 && std::string_view(argv[1]) != "--self-test") {
		return visionSample::runProfileCli(argc, argv, visionProduct::createApplication, "cuda.result");
	}
	if (argc < 2 || argc > 3) {
		std::cerr << "usage: openCvCudaSample <profile.json> | --self-test [device-index]\n";
		return 1;
	}
	if (std::string_view(argv[1]) != "--self-test") {
		std::cerr << "device-index is only accepted with --self-test; use cuda.device in the profile\n";
		return 1;
	}
	int device = 0;
	if (argc == 3) {
		const std::string_view text(argv[2]);
		const auto parsed = std::from_chars(text.data(), text.data() + text.size(), device);
		if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || device < 0) {
			std::cerr << "device-index must be a nonnegative integer\n";
			return 1;
		}
	}
	try {
		return runSelfTest(device);
	} catch (const std::exception& exception) {
		std::cerr << "CUDA sample failed: " << exception.what() << '\n';
		return 1;
	}
}
