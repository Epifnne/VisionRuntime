#include "camera/continuousCameraSource.hpp"

#include "core/status.hpp"
#include "core/tensorBuffer.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <utility>

namespace {

class FakeCameraDevice final : public visionRuntime::camera::ICameraDevice {
public:
	visionRuntime::core::Result<void> startAcquisition(
		visionRuntime::camera::CameraAcquisitionOptions options,
		visionRuntime::camera::FrameCallback callback) override {
		++startCount;
		lastOptions = options;
		if (failNextStart) {
			failNextStart = false;
			return visionRuntime::core::Result<void>::failure(
				visionRuntime::core::Status::error(
					visionRuntime::core::StatusCode::Unavailable,
					"fake start failure"));
		}
		callback_ = std::move(callback);
		acquiring = true;
		return visionRuntime::core::Result<void>::success();
	}

	void requestStop() noexcept override {
		++stopCount;
		acquiring = false;
	}

	void wait() noexcept override {
		++waitCount;
		acquiring = false;
		callback_ = {};
	}

	[[nodiscard]] bool isAcquiring() const noexcept override {
		return acquiring;
	}

	visionRuntime::core::Result<void> softwareTrigger() override {
		return visionRuntime::core::Result<void>::failure(
			visionRuntime::core::Status::error(
				visionRuntime::core::StatusCode::Unsupported,
				"fake device has no trigger"));
	}

	[[nodiscard]] const visionRuntime::camera::CameraDeviceInfo& deviceInfo()
		const noexcept override {
		return deviceInfo_;
	}

	[[nodiscard]] const visionRuntime::camera::CameraCapabilities& capabilities()
		const noexcept override {
		return capabilities_;
	}

	[[nodiscard]] visionRuntime::vision::FrameSpec outputSpec() const override {
		return {{visionRuntime::vision::PixelFormat::Gray8}, 1, 1,
			visionRuntime::core::Device::cpu()};
	}

	void emitFrame() {
		if (!callback_) {
			return;
		}
		auto buffer = visionRuntime::core::TensorBuffer::allocate(1);
		auto frame = visionRuntime::vision::Frame::create(
			std::move(buffer).value(), 1, 1,
			visionRuntime::vision::PixelFormat::Gray8);
		callback_(std::move(frame));
	}

	visionRuntime::camera::CameraAcquisitionOptions lastOptions;
	std::size_t startCount = 0;
	std::size_t stopCount = 0;
	std::size_t waitCount = 0;
	bool failNextStart = false;
	bool acquiring = false;

private:
	visionRuntime::camera::FrameCallback callback_;
	visionRuntime::camera::CameraDeviceInfo deviceInfo_;
	visionRuntime::camera::CameraCapabilities capabilities_;
};

} // namespace

TEST(ContinuousCameraSourceTest, RejectsMissingDeviceAndInvalidOptions) {
	using namespace visionRuntime;

	auto missingDevice = camera::ContinuousCameraSource::create(nullptr);
	ASSERT_FALSE(missingDevice);
	EXPECT_EQ(missingDevice.status().code(), core::StatusCode::InvalidArgument);

	auto invalidOptions = camera::ContinuousCameraSource::create(
		std::make_unique<FakeCameraDevice>(), {.frameRate = 0.0});
	ASSERT_FALSE(invalidOptions);
	EXPECT_EQ(invalidOptions.status().code(), core::StatusCode::InvalidArgument);
}

TEST(ContinuousCameraSourceTest, ForwardsContinuousAcquisitionAndLifecycle) {
	using namespace visionRuntime;

	auto device = std::make_unique<FakeCameraDevice>();
	auto* devicePointer = device.get();
	auto sourceResult = camera::ContinuousCameraSource::create(
		std::move(device), {.frameRate = 25.0});
	ASSERT_TRUE(sourceResult) << sourceResult.status().toString();
	auto source = std::move(sourceResult).value();

	const auto info = source->info();
	EXPECT_FALSE(info.isFinite);
	EXPECT_FALSE(info.expectedFrameCount);
	EXPECT_EQ(info.outputSpec.width, 1U);
	EXPECT_EQ(info.outputSpec.height, 1U);

	std::size_t frameCount = 0;
	ASSERT_TRUE(source->start([&](core::Result<vision::Frame> frame) {
		ASSERT_TRUE(frame) << frame.status().toString();
		++frameCount;
	}));
	EXPECT_TRUE(source->isRunning());
	EXPECT_EQ(devicePointer->startCount, 1U);
	EXPECT_EQ(devicePointer->lastOptions.mode, camera::AcquisitionMode::Continuous);
	EXPECT_EQ(devicePointer->lastOptions.frameRate, 25.0);

	devicePointer->emitFrame();
	EXPECT_EQ(frameCount, 1U);

	auto restarted = source->start([](core::Result<vision::Frame>) {});
	ASSERT_FALSE(restarted);
	EXPECT_EQ(restarted.status().code(), core::StatusCode::InvalidState);

	source->requestStop();
	source->requestStop();
	source->wait();
	source->wait();
	EXPECT_FALSE(source->isRunning());
	EXPECT_EQ(devicePointer->stopCount, 2U);
	EXPECT_EQ(devicePointer->waitCount, 2U);
	devicePointer->emitFrame();
	EXPECT_EQ(frameCount, 1U);
}

TEST(ContinuousCameraSourceTest, AllowsRetryAfterDeviceStartFailure) {
	using namespace visionRuntime;

	auto device = std::make_unique<FakeCameraDevice>();
	auto* devicePointer = device.get();
	devicePointer->failNextStart = true;
	auto sourceResult = camera::ContinuousCameraSource::create(std::move(device));
	ASSERT_TRUE(sourceResult);
	auto source = std::move(sourceResult).value();

	auto failed = source->start([](core::Result<vision::Frame>) {});
	ASSERT_FALSE(failed);
	EXPECT_EQ(failed.status().code(), core::StatusCode::Unavailable);
	EXPECT_TRUE(source->start([](core::Result<vision::Frame>) {}));
	EXPECT_EQ(devicePointer->startCount, 2U);
}
