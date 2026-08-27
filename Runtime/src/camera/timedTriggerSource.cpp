#include "camera/timedTriggerSource.hpp"

#include "core/status.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace visionRuntime::camera {
namespace {

[[nodiscard]] core::Status error(core::StatusCode code, const char* message) {
	return core::Status::error(code, message);
}

} // namespace

class TimedTriggerSource::Impl {
public:
	Impl(std::unique_ptr<ICameraDevice> device, TimedTriggerSourceOptions options)
		: device_(std::move(device)), options_(options) {}

	~Impl() {
		requestStop();
		wait();
	}

	[[nodiscard]] core::Result<void> start(FrameCallback callback) {
		std::scoped_lock lifecycleLock(lifecycleMutex_);
		if (started_) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidState,
				"timed trigger source has already started"));
		}
		if (!callback) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidArgument,
				"frame callback must not be empty"));
		}

		{
			std::scoped_lock stateLock(stateMutex_);
			callback_ = std::move(callback);
			activated_ = false;
			stopRequested_ = false;
			responseArrived_ = false;
			callbackCompleted_ = false;
			terminal_ = false;
		}
		try {
			worker_ = std::jthread([this](std::stop_token) { run(); });
		} catch (...) {
			callback_ = {};
			return core::Result<void>::failure(error(
				core::StatusCode::ResourceExhausted,
				"failed to create timed trigger scheduling thread"));
		}

		auto acquired = device_->startAcquisition({
			.mode = AcquisitionMode::SoftwareTrigger,
			.frameRate = std::nullopt,
		}, [this](core::Result<vision::Frame> frame) {
			onDeviceFrame(std::move(frame));
		});
		if (!acquired) {
			{
				std::scoped_lock stateLock(stateMutex_);
				stopRequested_ = true;
			}
			stateReady_.notify_all();
			worker_.join();
			callback_ = {};
			return acquired;
		}

		started_ = true;
		running_ = true;
		{
			std::scoped_lock stateLock(stateMutex_);
			activated_ = true;
		}
		stateReady_.notify_all();
		return core::Result<void>::success();
	}

	void requestStop() noexcept {
		{
			std::scoped_lock lock(stateMutex_);
			stopRequested_ = true;
		}
		running_ = false;
		stateReady_.notify_all();
		device_->requestStop();
	}

	void wait() noexcept {
		std::scoped_lock waitLock(waitMutex_);
		if (worker_.joinable()) {
			if (worker_.get_id() == std::this_thread::get_id()) {
				return;
			}
			worker_.join();
		}
		device_->wait();
		running_ = false;
	}

	[[nodiscard]] bool isRunning() const noexcept {
		return running_;
	}

	[[nodiscard]] FrameSourceInfo info() const {
		return {
			.outputSpec = device_->outputSpec(),
			.expectedFrameCount = std::nullopt,
			.isFinite = false,
		};
	}

private:
	void run() noexcept {
		try {
			std::unique_lock lock(stateMutex_);
			stateReady_.wait(lock, [this] { return activated_ || stopRequested_; });
			while (!stopRequested_ && !terminal_) {
				responseArrived_ = false;
				callbackCompleted_ = false;
				responseTime_ = {};
				lock.unlock();
				auto triggered = device_->softwareTrigger();
				lock.lock();
				if (!triggered && !responseArrived_) {
					terminal_ = true;
					auto status = triggered.status().withContext("software trigger failed");
					lock.unlock();
					deliverError(std::move(status));
					lock.lock();
					break;
				}

				const bool received = stateReady_.wait_for(lock,
					options_.responseTimeout,
					[this] { return responseArrived_ || stopRequested_ || terminal_; });
				if (stopRequested_) {
					break;
				}
				if (!received || (!responseArrived_ && !terminal_)) {
					terminal_ = true;
					lock.unlock();
					deliverError(error(core::StatusCode::DeadlineExceeded,
						"camera did not respond to software trigger before timeout"));
					lock.lock();
					break;
				}
				stateReady_.wait(lock,
					[this] { return callbackCompleted_ || stopRequested_; });
				if (stopRequested_ || terminal_) {
					break;
				}
				const auto nextFrameDeadline = responseTime_ + options_.triggerInterval;
				stateReady_.wait_until(lock, nextFrameDeadline,
					[this] { return stopRequested_ || terminal_; });
			}
		} catch (...) {
		}
		running_ = false;
		device_->requestStop();
	}

	void onDeviceFrame(core::Result<vision::Frame> frame) noexcept {
		const bool succeeded = static_cast<bool>(frame);
		{
			std::scoped_lock lock(stateMutex_);
			if (stopRequested_ || terminal_ || (succeeded && responseArrived_)) {
				return;
			}
			if (succeeded) {
				responseArrived_ = true;
				responseTime_ = std::chrono::steady_clock::now();
			} else {
				terminal_ = true;
			}
		}
		stateReady_.notify_all();

		try {
			callback_(std::move(frame));
		} catch (...) {
		}

		{
			std::scoped_lock lock(stateMutex_);
			callbackCompleted_ = true;
		}
		stateReady_.notify_all();
	}

	void deliverError(core::Status status) noexcept {
		try {
			callback_(core::Result<vision::Frame>::failure(std::move(status)));
		} catch (...) {
		}
	}

	std::unique_ptr<ICameraDevice> device_;
	TimedTriggerSourceOptions options_;
	FrameCallback callback_;
	std::mutex lifecycleMutex_;
	std::mutex waitMutex_;
	std::jthread worker_;
	std::atomic_bool running_ = false;
	bool started_ = false;
	std::mutex stateMutex_;
	std::condition_variable stateReady_;
	bool activated_ = false;
	bool stopRequested_ = false;
	bool responseArrived_ = false;
	bool callbackCompleted_ = false;
	bool terminal_ = false;
	std::chrono::steady_clock::time_point responseTime_;
};

core::Result<std::unique_ptr<TimedTriggerSource>> TimedTriggerSource::create(
	std::unique_ptr<ICameraDevice> device, TimedTriggerSourceOptions options) {
	if (!device) {
		return core::Result<std::unique_ptr<TimedTriggerSource>>::failure(error(
			core::StatusCode::InvalidArgument,
			"timed trigger source requires a device"));
	}
	if (!options.isValid()) {
		return core::Result<std::unique_ptr<TimedTriggerSource>>::failure(error(
			core::StatusCode::InvalidArgument,
			"invalid timed trigger source options"));
	}
	if (!device->capabilities().supportsSoftwareTrigger) {
		return core::Result<std::unique_ptr<TimedTriggerSource>>::failure(error(
			core::StatusCode::Unsupported,
			"camera device does not support software trigger"));
	}
	auto impl = std::make_unique<Impl>(std::move(device), options);
	return core::Result<std::unique_ptr<TimedTriggerSource>>::success(
		std::unique_ptr<TimedTriggerSource>(
			new TimedTriggerSource(std::move(impl))));
}

TimedTriggerSource::TimedTriggerSource(std::unique_ptr<Impl> impl) noexcept
	: impl_(std::move(impl)) {}

TimedTriggerSource::~TimedTriggerSource() = default;

core::Result<void> TimedTriggerSource::start(FrameCallback callback) {
	return impl_->start(std::move(callback));
}

void TimedTriggerSource::requestStop() noexcept {
	impl_->requestStop();
}

void TimedTriggerSource::wait() noexcept {
	impl_->wait();
}

bool TimedTriggerSource::isRunning() const noexcept {
	return impl_->isRunning();
}

FrameSourceInfo TimedTriggerSource::info() const {
	return impl_->info();
}

} // namespace visionRuntime::camera
