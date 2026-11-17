#pragma once

#include "camera/iCameraDevice.hpp"
#include "camera/iFrameSource.hpp"
#include "../common/profileSupport.hpp"

#include <condition_variable>
#include <mutex>

namespace eventInspectionSample {

// Gate FileSource delivery with capture requests, preserving event identities.
class ReplayCamera final : public visionRuntime::camera::ICameraDevice {
public:
	explicit ReplayCamera(std::unique_ptr<visionRuntime::camera::IFrameSource> source)
		: source_(std::move(source)) { capabilities_.supportsSoftwareTrigger = true; }
	~ReplayCamera() override { requestStop(); wait(); }

	visionRuntime::core::Result<void> startAcquisition(
		visionRuntime::camera::CameraAcquisitionOptions options,
		visionRuntime::camera::FrameCallback callback) override {
		{
			std::lock_guard lock(mutex_);
			if (started_ || stopped_ || !callback ||
				options.mode != visionRuntime::camera::AcquisitionMode::SoftwareTrigger) {
				return visionSample::profileError<void>("replay requires one software-trigger acquisition");
			}
			started_ = true;
		}
		return source_->start([this, callback = std::move(callback)](auto frame) {
			{
				std::unique_lock lock(mutex_);
				ready_.wait(lock, [this] { return triggered_ || stopped_; });
				if (stopped_) { return; }
				triggered_ = false;
			}
			callback(std::move(frame));
		});
	}
	visionRuntime::core::Result<void> softwareTrigger() override {
		{
			std::lock_guard lock(mutex_);
			if (!started_ || stopped_ || triggered_ || !source_->isRunning()) {
				return visionSample::profileError<void>("replay is stopped, exhausted or already triggered");
			}
			triggered_ = true;
		}
		ready_.notify_one();
		return visionRuntime::core::Result<void>::success();
	}
	void requestStop() noexcept override {
		{
			std::lock_guard lock(mutex_);
			stopped_ = true;
		}
		ready_.notify_all();
		source_->requestStop();
	}
	void wait() noexcept override { source_->wait(); }
	bool isAcquiring() const noexcept override { return source_->isRunning(); }
	const visionRuntime::camera::CameraDeviceInfo& deviceInfo() const noexcept override { return info_; }
	const visionRuntime::camera::CameraCapabilities& capabilities() const noexcept override {
		return capabilities_;
	}
	visionRuntime::vision::FrameSpec outputSpec() const override { return source_->info().outputSpec; }

private:
	std::unique_ptr<visionRuntime::camera::IFrameSource> source_;
	visionRuntime::camera::CameraDeviceInfo info_;
	visionRuntime::camera::CameraCapabilities capabilities_;
	std::mutex mutex_;
	std::condition_variable ready_;
	bool started_ = false;
	bool stopped_ = false;
	bool triggered_ = false;
};

} // namespace eventInspectionSample
