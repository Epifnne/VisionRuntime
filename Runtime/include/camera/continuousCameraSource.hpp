#pragma once

#include "camera/cameraSourceOptions.hpp"
#include "camera/iCameraDevice.hpp"
#include "camera/iFrameSource.hpp"

#include <memory>

namespace visionRuntime::camera {

class ContinuousCameraSource final : public IFrameSource {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<ContinuousCameraSource>> create(
		std::unique_ptr<ICameraDevice> device,
		ContinuousCameraSourceOptions options = {});

	~ContinuousCameraSource() override;

	ContinuousCameraSource(const ContinuousCameraSource&) = delete;
	ContinuousCameraSource& operator=(const ContinuousCameraSource&) = delete;
	ContinuousCameraSource(ContinuousCameraSource&&) = delete;
	ContinuousCameraSource& operator=(ContinuousCameraSource&&) = delete;

	core::Result<void> start(FrameCallback callback) override;
	void requestStop() noexcept override;
	void wait() noexcept override;
	[[nodiscard]] bool isRunning() const noexcept override;
	[[nodiscard]] FrameSourceInfo info() const override;

private:
	class Impl;

	explicit ContinuousCameraSource(std::unique_ptr<Impl> impl) noexcept;

	std::unique_ptr<Impl> impl_;
};

} // namespace visionRuntime::camera
