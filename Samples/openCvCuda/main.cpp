#include "cudaThresholdAlgorithm.hpp"

#include "camera/fileSource.hpp"
#include "pipeline/openCvPipelineBuilder.hpp"
#include "runtime/runtimeFactory.hpp"

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

int run(std::string_view input, int device) {
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
	if (input == "--self-test") {
		return selfTest(built.value());
	}
	auto source = camera::FileSource::create({.directory = std::filesystem::path(input)});
	if (!source) {
		std::cerr << source.status().toString() << '\n';
		return 1;
	}
	executor::FrameExecutionOptions<InspectionResult> options;
	options.sourceFailurePolicy = executor::SourceFailurePolicy::Stop;
	options.sourceFailureCallback = [](const core::Status& status) {
		std::cerr << "source: " << status.toString() << '\n';
	};
	options.completionCallback = [](executor::TaskId taskId,
		const core::Result<InspectionResult>& result) {
		if (!result) {
			std::cerr << "task=" << taskId << ": " << result.status().toString() << '\n';
			return;
		}
		std::cout << "sequence=" << result->sequence
			<< " brightFraction=" << result->brightFraction
			<< " accepted=" << result->accepted << '\n';
	};
	config::DeploymentConfig deployment;
	deployment.executor.performancePolicy = config::PerformancePolicy::Serial;
	deployment.executor.queueFullPolicy = config::QueueFullPolicy::Block;
	auto created = runtime::RuntimeFactory::createRuntime<InspectionResult>(
		std::move(source).value(),
		std::make_unique<pipeline::OpenCvPipeline<InspectionResult>>(std::move(built).value()),
		deployment, std::move(options));
	if (!created) {
		std::cerr << created.status().toString() << '\n';
		return 1;
	}
	auto session = std::move(created).value();
	auto started = session->start();
	if (!started) {
		std::cerr << started.status().toString() << '\n';
		return 1;
	}
	const auto summary = session->wait();
	std::cout << "completed=" << summary.completed << " failed=" << summary.failed
		<< " dropped=" << summary.dropped << " sourceFailures=" << summary.sourceFailures << '\n';
	return summary.completed == 0 || summary.failed != 0 ||
		summary.dropped != 0 || summary.sourceFailures != 0 ? 1 : 0;
}
} // namespace

int main(int argc, char* argv[]) {
	if (argc < 2 || argc > 3) {
		std::cerr << "usage: openCvCudaSample <Gray8-image-directory|--self-test> [device-index]\n";
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
		return run(argv[1], device);
	} catch (const std::exception& exception) {
		std::cerr << "CUDA sample failed: " << exception.what() << '\n';
		return 1;
	}
}
