#pragma once

#include "camera/cameraSourceOptions.hpp"
#include "camera/iCameraDevice.hpp"
#include "camera/iFrameSource.hpp"

#include <memory>

namespace visionRuntime::camera {

class TimedTriggerSource final : public IFrameSource {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<TimedTriggerSource>> create(
		std::unique_ptr<ICameraDevice> device,
		TimedTriggerSourceOptions options = {});

	~TimedTriggerSource() override;

	TimedTriggerSource(const TimedTriggerSource&) = delete;
	TimedTriggerSource& operator=(const TimedTriggerSource&) = delete;
	TimedTriggerSource(TimedTriggerSource&&) = delete;
	TimedTriggerSource& operator=(TimedTriggerSource&&) = delete;

	core::Result<void> start(FrameCallback callback) override;
	void requestStop() noexcept override;
	void wait() noexcept override;
	[[nodiscard]] bool isRunning() const noexcept override;
	[[nodiscard]] FrameSourceInfo info() const override;

private:
	class Impl;

	explicit TimedTriggerSource(std::unique_ptr<Impl> impl) noexcept;

	std::unique_ptr<Impl> impl_;
};

} // namespace visionRuntime::camera
