#include "camera/continuousCameraSource.hpp"

#include "core/status.hpp"
#include "logs/logger.hpp"

#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include <utility>

namespace visionRuntime::camera {
namespace {

[[nodiscard]] core::Status error(core::StatusCode code, const char* message) {
	return core::Status::error(code, message);
}

} // namespace

class ContinuousCameraSource::Impl {
public:
	Impl(std::unique_ptr<ICameraDevice> device, ContinuousCameraSourceOptions options)
		: device_(std::move(device)), options_(std::move(options)) {}

	~Impl() {
		requestStop();
		wait();
	}

	[[nodiscard]] core::Result<void> start(FrameCallback callback) {
		std::scoped_lock lock(mutex_);
		if (started_) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidState,
				"continuous camera source has already started"));
		}
		if (!callback) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidArgument,
				"frame callback must not be empty"));
		}
		callback_ = std::move(callback);
		stopping_ = false;
		terminal_ = false;
		{
			std::lock_guard watchLock(watchMutex_);
			lastFrame_ = std::chrono::steady_clock::now();
		}
		auto started = device_->startAcquisition({
			.mode = AcquisitionMode::Continuous,
			.frameRate = options_.frameRate,
		}, [this](core::Result<vision::Frame> frame) {
			{
				std::lock_guard watchLock(watchMutex_);
				lastFrame_ = std::chrono::steady_clock::now();
			}
			watchReady_.notify_all();
			std::lock_guard deliveryLock(deliveryMutex_);
			if (terminal_ || stopping_) {
				return;
			}
			if (!frame) {
				terminal_ = true;
				watchReady_.notify_all();
			}
			deliver(std::move(frame));
		});
		if (!started) {
			return started;
		}
		started_ = true;
		if (options_.noFrameTimeout) {
			try {
				watchThread_ = std::thread([this] { watchFrames(); });
			} catch (const std::exception& exception) {
				requestStop();
				device_->wait();
				auto status = core::Status::error(core::StatusCode::ResourceExhausted,
					std::string("failed to start camera watchdog: ") + exception.what());
				logs::report(status);
				return core::Result<void>::failure(std::move(status));
			}
		}
		return core::Result<void>::success();
	}

	void requestStop() noexcept {
		stopping_ = true;
		watchReady_.notify_all();
		device_->requestStop();
	}

	void wait() noexcept {
		device_->wait();
		stopping_ = true;
		watchReady_.notify_all();
		std::lock_guard joinLock(joinMutex_);
		if (watchThread_.joinable() && watchThread_.get_id() != std::this_thread::get_id()) {
			watchThread_.join();
		}
	}

	[[nodiscard]] bool isRunning() const noexcept {
		return device_->isAcquiring();
	}
	[[nodiscard]] ICameraDevice* cameraDevice() const noexcept { return device_.get(); }

	[[nodiscard]] FrameSourceInfo info() const {
		return {
			.outputSpec = device_->outputSpec(),
			.expectedFrameCount = std::nullopt,
			.isFinite = false,
		};
	}

private:
	void deliver(core::Result<vision::Frame> frame) noexcept {
		try {
			callback_(std::move(frame));
		} catch (...) {
			logs::error("continuous camera callback threw an exception");
		}
	}

	void watchFrames() noexcept {
		std::unique_lock lock(watchMutex_);
		while (!stopping_ && !terminal_) {
			const auto observed = lastFrame_;
			if (watchReady_.wait_until(lock, observed + *options_.noFrameTimeout,
				[this, observed] { return stopping_ || terminal_ || lastFrame_ != observed; })) {
				continue;
			}
			terminal_ = true;
			lock.unlock();
			device_->requestStop();
			std::lock_guard deliveryLock(deliveryMutex_);
			deliver(core::Result<vision::Frame>::failure(error(
				core::StatusCode::DeadlineExceeded,
				"continuous camera delivered no frame before its silence timeout")));
			return;
		}
	}

	std::unique_ptr<ICameraDevice> device_;
	ContinuousCameraSourceOptions options_;
	std::mutex mutex_;
	bool started_ = false;
	FrameCallback callback_;
	std::atomic_bool stopping_{false};
	std::atomic_bool terminal_{false};
	std::mutex deliveryMutex_;
	std::mutex watchMutex_;
	std::condition_variable watchReady_;
	std::chrono::steady_clock::time_point lastFrame_;
	std::thread watchThread_;
	std::mutex joinMutex_;
};

core::Result<std::unique_ptr<ContinuousCameraSource>> ContinuousCameraSource::create(
	std::unique_ptr<ICameraDevice> device, ContinuousCameraSourceOptions options) {
	if (!device) {
		return core::Result<std::unique_ptr<ContinuousCameraSource>>::failure(error(
			core::StatusCode::InvalidArgument,
			"continuous camera source requires a device"));
	}
	if (!options.isValid()) {
		return core::Result<std::unique_ptr<ContinuousCameraSource>>::failure(error(
			core::StatusCode::InvalidArgument,
			"invalid continuous camera source options"));
	}
	auto impl = std::make_unique<Impl>(std::move(device), std::move(options));
	return core::Result<std::unique_ptr<ContinuousCameraSource>>::success(
		std::unique_ptr<ContinuousCameraSource>(
			new ContinuousCameraSource(std::move(impl))));
}

ContinuousCameraSource::ContinuousCameraSource(std::unique_ptr<Impl> impl) noexcept
	: impl_(std::move(impl)) {}

ContinuousCameraSource::~ContinuousCameraSource() = default;

core::Result<void> ContinuousCameraSource::start(FrameCallback callback) {
	return impl_->start(std::move(callback));
}

void ContinuousCameraSource::requestStop() noexcept {
	impl_->requestStop();
}

void ContinuousCameraSource::wait() noexcept {
	impl_->wait();
}

bool ContinuousCameraSource::isRunning() const noexcept {
	return impl_->isRunning();
}

FrameSourceInfo ContinuousCameraSource::info() const {
	return impl_->info();
}

ICameraDevice* ContinuousCameraSource::cameraDevice() const noexcept {
	return impl_->cameraDevice();
}

} // namespace visionRuntime::camera
