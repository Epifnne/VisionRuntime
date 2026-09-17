#include "camera/iCameraDevice.hpp"

#include "core/status.hpp"

#include <gtest/gtest.h>

namespace {

class StubCameraDevice final : public visionRuntime::camera::ICameraDevice {
public:
	visionRuntime::core::Result<void> startAcquisition(
		visionRuntime::camera::CameraAcquisitionOptions,
		visionRuntime::camera::FrameCallback) override {
		return visionRuntime::core::Result<void>::failure(
			visionRuntime::core::Status::error(
				visionRuntime::core::StatusCode::Unsupported,
				"stub device never starts acquisition"));
	}

	void requestStop() noexcept override {}
	void wait() noexcept override {}
	[[nodiscard]] bool isAcquiring() const noexcept override { return false; }

	visionRuntime::core::Result<void> softwareTrigger() override {
		return visionRuntime::core::Result<void>::failure(
			visionRuntime::core::Status::error(
				visionRuntime::core::StatusCode::Unsupported,
				"stub device has no software trigger"));
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

private:
	visionRuntime::camera::CameraDeviceInfo deviceInfo_;
	visionRuntime::camera::CameraCapabilities capabilities_;
};

} // namespace

TEST(CameraDeviceContractTest, ExposureSetterDefaultsToUnsupported) {
	using namespace visionRuntime;

	StubCameraDevice device;
	const auto result = device.setExposureMicroseconds(1000.0);
	ASSERT_FALSE(result);
	EXPECT_EQ(result.status().code(), core::StatusCode::Unsupported);
}

TEST(CameraDeviceContractTest, GainSetterDefaultsToUnsupported) {
	using namespace visionRuntime;

	StubCameraDevice device;
	const auto result = device.setGain(2.0);
	ASSERT_FALSE(result);
	EXPECT_EQ(result.status().code(), core::StatusCode::Unsupported);
}
