#pragma once

#include "camera/iCameraDevice.hpp"
#include "memory/cpuAllocator.hpp"

#include <condition_variable>
#include <mutex>

namespace eventInspectionSample {

class SimulatedCamera final : public visionRuntime::camera::ICameraDevice {
public:
	explicit SimulatedCamera(bool sideView) : sideView_(sideView) {
		capabilities_.supportsSoftwareTrigger = true;
	}

	visionRuntime::core::Result<void> startAcquisition(
		visionRuntime::camera::CameraAcquisitionOptions options,
		visionRuntime::camera::FrameCallback callback) override {
		using namespace visionRuntime;
		std::lock_guard lock(mutex_);
		if (started_ || !callback || options.mode != camera::AcquisitionMode::SoftwareTrigger) {
			return core::Result<void>::failure(core::Status::error(
				core::StatusCode::InvalidState, "simulation requires a single software-trigger acquisition"));
		}
		callback_ = std::move(callback);
		started_ = true;
		running_ = true;
		return core::Result<void>::success();
	}

	visionRuntime::core::Result<void> softwareTrigger() override {
		using namespace visionRuntime;
		camera::FrameCallback callback;
		std::uint64_t sequence;
		{
			std::lock_guard lock(mutex_);
			if (!running_) {
				return core::Result<void>::failure(core::Status::error(
					core::StatusCode::InvalidState, "simulated camera is stopped"));
			}
			callback = callback_;
			sequence = sequence_++;
			++callbacks_;
		}
		auto buffer = memory::CpuAllocator{}.allocate(4);
		if (buffer) {
			auto* bytes = static_cast<std::uint8_t*>(buffer->data());
			const std::uint8_t brightness = sequence % 2 ? 200 : 20;
			for (std::size_t index = 0; index < 4; ++index) {
				bytes[index] = sideView_ ? (index % 2 ? 255 : 0) : brightness;
			}
			callback(vision::Frame::create(std::move(buffer).value(), 2, 2,
				vision::PixelFormat::Gray8, 2, {
					.sequenceNumber = sequence, .capturedAt = std::chrono::steady_clock::now()}));
		} else {
			callback(core::Result<vision::Frame>::failure(buffer.status()));
		}
		{
			std::lock_guard lock(mutex_);
			--callbacks_;
		}
		ready_.notify_all();
		return core::Result<void>::success();
	}

	void requestStop() noexcept override {
		std::lock_guard lock(mutex_);
		running_ = false;
	}
	void wait() noexcept override {
		std::unique_lock lock(mutex_);
		ready_.wait(lock, [this] { return callbacks_ == 0; });
		callback_ = {};
	}
	bool isAcquiring() const noexcept override {
		std::lock_guard lock(mutex_);
		return running_;
	}
	const visionRuntime::camera::CameraDeviceInfo& deviceInfo() const noexcept override { return info_; }
	const visionRuntime::camera::CameraCapabilities& capabilities() const noexcept override {
		return capabilities_;
	}
	visionRuntime::vision::FrameSpec outputSpec() const override {
		return {{visionRuntime::vision::PixelFormat::Gray8}, 2, 2, visionRuntime::core::Device::cpu()};
	}

private:
	bool sideView_;
	mutable std::mutex mutex_;
	std::condition_variable ready_;
	visionRuntime::camera::FrameCallback callback_;
	visionRuntime::camera::CameraDeviceInfo info_;
	visionRuntime::camera::CameraCapabilities capabilities_;
	std::uint64_t sequence_ = 0;
	std::size_t callbacks_ = 0;
	bool started_ = false;
	bool running_ = false;
};

} // namespace eventInspectionSample
