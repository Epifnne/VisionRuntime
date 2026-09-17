#include "camera/hikrobotMvsCameraDevice.hpp"
#include "core/result.hpp"
#include "core/status.hpp"
#include "vision/frame.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

int main(int argc, char** argv) {
	using namespace std::chrono_literals;
	using namespace visionRuntime;

	const auto devices = camera::HikrobotMvsCameraDevice::enumerate();
	if (!devices) {
		std::cerr << devices.status().toString() << '\n';
		return EXIT_FAILURE;
	}
	for (const auto& device : *devices) {
		std::cout << device.serialNumber << ',' << device.modelName << ','
			<< device.userDefinedName;
		if (device.ipAddress) {
			std::cout << ',' << *device.ipAddress;
		}
		std::cout << '\n';
	}
	if (devices->empty()) {
		std::cerr << "no MVS camera found\n";
		return EXIT_FAILURE;
	}

	camera::CameraDeviceOptions options;
	if (argc > 1) {
		options.serialNumber = argv[1];
	}
	const bool softwareTrigger = argc > 2 && std::string(argv[2]) == "trigger";
	const auto acquisitionMode = softwareTrigger
		? camera::AcquisitionMode::SoftwareTrigger
		: camera::AcquisitionMode::Continuous;
	auto sourceResult = camera::HikrobotMvsCameraDevice::create(std::move(options));
	if (!sourceResult) {
		std::cerr << sourceResult.status().toString() << '\n';
		return EXIT_FAILURE;
	}
	auto source = std::move(sourceResult).value();

	const auto negativeExposure = source->setExposureMicroseconds(-1.0);
	if (negativeExposure ||
		negativeExposure.status().code() != core::StatusCode::InvalidArgument) {
		std::cerr << "negative exposure was not rejected with InvalidArgument\n";
		return EXIT_FAILURE;
	}
	const std::optional<double> runtimeExposure =
		argc > 3 ? std::optional<double>(std::stod(argv[3])) : std::nullopt;
	const std::optional<double> runtimeGain =
		argc > 4 ? std::optional<double>(std::stod(argv[4])) : std::nullopt;

	std::mutex mutex;
	std::condition_variable frameReady;
	std::size_t frameCount = 0;
	bool failed = false;
	auto started = source->startAcquisition(
		{.mode = acquisitionMode}, [&](core::Result<vision::Frame> frame) {
		std::scoped_lock lock(mutex);
		if (!frame) {
			std::cerr << frame.status().toString() << '\n';
			failed = true;
		} else {
			std::cout << "frame=" << frame->metadata().sequenceNumber
				<< ",size=" << frame->width() << 'x' << frame->height()
				<< ",bytes=" << frame->byteSize() << '\n';
			++frameCount;
		}
		frameReady.notify_one();
	});
	if (!started) {
		std::cerr << started.status().toString() << '\n';
		return EXIT_FAILURE;
	}

	if (runtimeExposure) {
		const auto applied = source->setExposureMicroseconds(*runtimeExposure);
		if (!applied) {
			std::cerr << applied.status().toString() << '\n';
			source->requestStop();
			source->wait();
			return EXIT_FAILURE;
		}
	}
	if (runtimeGain) {
		const auto applied = source->setGain(*runtimeGain);
		if (!applied) {
			std::cerr << applied.status().toString() << '\n';
			source->requestStop();
			source->wait();
			return EXIT_FAILURE;
		}
	}

	for (std::size_t index = 0; index < 10; ++index) {
		{
			std::scoped_lock lock(mutex);
			if (failed) {
				break;
			}
		}
		if (softwareTrigger) {
			auto triggered = source->softwareTrigger();
			if (!triggered) {
				std::cerr << triggered.status().toString() << '\n';
				std::scoped_lock lock(mutex);
				failed = true;
				break;
			}
		}
		std::unique_lock lock(mutex);
		const auto expected = index + 1;
		if (!frameReady.wait_for(lock, 5s, [&] {
			return frameCount >= expected || failed;
		})) {
			std::cerr << "timed out waiting for frame\n";
			failed = true;
		}
	}

	source->requestStop();
	source->wait();
	return failed || frameCount == 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}