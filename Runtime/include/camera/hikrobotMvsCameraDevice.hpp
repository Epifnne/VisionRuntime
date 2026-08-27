#pragma once

#include "camera/iCameraDevice.hpp"

#include <memory>
#include <vector>

namespace visionRuntime::camera {

class HikrobotMvsCameraDevice final : public ICameraDevice {
public:
	[[nodiscard]] static core::Result<std::vector<CameraDeviceInfo>> enumerate();
	[[nodiscard]] static core::Result<std::unique_ptr<HikrobotMvsCameraDevice>> create(
		CameraDeviceOptions options);

	~HikrobotMvsCameraDevice() override;

	HikrobotMvsCameraDevice(const HikrobotMvsCameraDevice&) = delete;
	HikrobotMvsCameraDevice& operator=(const HikrobotMvsCameraDevice&) = delete;
	HikrobotMvsCameraDevice(HikrobotMvsCameraDevice&&) = delete;
	HikrobotMvsCameraDevice& operator=(HikrobotMvsCameraDevice&&) = delete;

	core::Result<void> startAcquisition(
		CameraAcquisitionOptions options, FrameCallback callback) override;
	void requestStop() noexcept override;
	void wait() noexcept override;
	[[nodiscard]] bool isAcquiring() const noexcept override;

	core::Result<void> softwareTrigger() override;
	[[nodiscard]] const CameraDeviceInfo& deviceInfo() const noexcept override;
	[[nodiscard]] const CameraCapabilities& capabilities() const noexcept override;
	[[nodiscard]] vision::FrameSpec outputSpec() const override;

private:
	class Impl;

	explicit HikrobotMvsCameraDevice(std::unique_ptr<Impl> impl) noexcept;

	std::unique_ptr<Impl> impl_;
};

} // namespace visionRuntime::camera