#include "routingAlgorithm.hpp"

#include "camera/fileSource.hpp"
#include "pipeline/openCvPipelineBuilder.hpp"
#include "runtime/runtimeFactory.hpp"

#include <opencv2/imgcodecs.hpp>

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using namespace visionRuntime;
using namespace visionSample::yoloRouting;

void onClassification(executor::TaskId taskId, const Detection& object) {
	std::cout << "classification task=" << taskId << " label=" << object.label
		<< " confidence=" << object.confidence << " box=" << object.box << '\n';
}

int run(const std::filesystem::path& input, const std::filesystem::path& output) {
	std::error_code error;
	std::filesystem::create_directories(output, error);
	if (error) {
		std::cerr << "Cannot create output directory: " << error.message() << '\n';
		return 1;
	}
	auto source = camera::FileSource::create({.directory = input});
	if (!source) {
		std::cerr << source.status().toString() << '\n';
		return 1;
	}
	pipeline::OpenCvPipelineBuilder<InspectionResult> builder;
	auto built = builder.setAlgorithm(std::make_unique<RoutingAlgorithm>()).build();
	if (!built) {
		std::cerr << built.status().toString() << '\n';
		return 1;
	}

	bool outputFailed = false;
	executor::FrameExecutionOptions<InspectionResult> options;
	options.sourceFailurePolicy = executor::SourceFailurePolicy::Stop;
	options.sourceFailureCallback = [](const core::Status& status) {
		std::cerr << "source: " << status.toString() << '\n';
	};
	options.completionCallback = [&](executor::TaskId taskId,
		const core::Result<InspectionResult>& completed) {
		if (!completed) {
			std::cerr << "task=" << taskId << ": " << completed.status().toString() << '\n';
			return;
		}
		const auto& result = completed.value();
		for (const auto& object : result.objects) {
			onClassification(taskId, object);
		}
		for (const auto& defect : result.defects) {
			std::cout << "defect task=" << taskId << " label=" << defect.label
				<< " confidence=" << defect.confidence << " box=" << defect.box << '\n';
		}
		for (const auto& text : result.texts) {
			std::cout << "ocr task=" << taskId << " box=" << text.detection.box << ' ';
			if (text.recognition) {
				std::cout << "text=" << text.recognition.value().text
					<< " confidence=" << text.recognition.value().confidence << '\n';
			} else {
				std::cout << text.recognition.status().toString() << '\n';
			}
		}
		const auto path = output / ("frame-" + std::to_string(taskId) + ".png");
		try {
			if (!cv::imwrite(path.string(), result.annotatedImage)) {
				outputFailed = true;
				std::cerr << "Cannot write annotation: " << path << '\n';
			}
		} catch (const cv::Exception& exception) {
			outputFailed = true;
			std::cerr << "Cannot write annotation " << path << ": " << exception.what() << '\n';
		}
	};
	config::DeploymentConfig deployment;
	deployment.executor.performancePolicy = config::PerformancePolicy::Serial;
	deployment.executor.queueFullPolicy = config::QueueFullPolicy::Block;
	auto created = runtime::RuntimeFactory::createRuntime<InspectionResult>(
		std::move(source).value(),
		std::make_unique<pipeline::OpenCvPipeline<InspectionResult>>(
			std::move(built).value()),
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
		<< " dropped=" << summary.dropped << " sourceFailures=" << summary.sourceFailures
		<< '\n';
	return outputFailed || summary.failed != 0 || summary.dropped != 0 ||
		summary.sourceFailures != 0 || summary.completed == 0 ? 1 : 0;
}
} // namespace

int main(int argc, char* argv[]) {
	if (argc != 3) {
		std::cerr << "usage: yoloRoutingSample <image-directory> <annotation-directory>\n";
		return 1;
	}
	std::cout << "SIMULATED YOLO: boxes and object classes are synthetic; OCR is a placeholder.\n";
	try {
		return run(argv[1], argv[2]);
	} catch (const std::exception& exception) {
		std::cerr << "sample failed: " << exception.what() << '\n';
		return 1;
	}
}
