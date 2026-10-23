#include "camera/eventTriggerSource.hpp"
#include "logs/logger.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>

namespace visionRuntime::camera {
namespace {

core::Status error(core::StatusCode code, const char* message) {
	return core::Status::error(code, message);
}

} // namespace

class EventTriggerSource::Impl {
public:
	Impl(std::unique_ptr<ICameraDevice> device, EventTriggerOptions options)
		: device_(std::move(device)), options_(options) {}

	core::Result<void> start(CaptureCallback callback, std::function<void(const core::Status&)> faultObserver) {
		std::lock_guard lifecycle(lifecycleMutex_);
		if (started_) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidState, "event source is single-use"));
		}
		if (!callback) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidArgument, "capture callback must not be empty"));
		}
		callback_ = std::move(callback);
		faultObserver_ = std::move(faultObserver);
		started_ = true;
		auto acquired = device_->startAcquisition(
			{.mode = AcquisitionMode::SoftwareTrigger},
			[this](core::Result<vision::Frame> frame) { onFrame(std::move(frame)); });
		if (!acquired) {
			device_->requestStop();
			device_->wait();
			return acquired;
		}
		try {
			std::lock_guard lock(mutex_);
			worker_ = std::thread([this] { run(); });
			accepting_ = true;
		} catch (const std::exception& exception) {
			device_->requestStop();
			device_->wait();
			return core::Result<void>::failure(core::Status::error(
				core::StatusCode::ResourceExhausted,
				std::string("cannot start event capture worker: ") + exception.what()));
		}
		return core::Result<void>::success();
	}

	core::Result<void> request(CaptureRequest request) {
		std::lock_guard lock(mutex_);
		if (!accepting_ || stopped_ || fault_) {
			return core::Result<void>::failure(fault_.value_or(error(
				core::StatusCode::InvalidState, "event source is not accepting captures")));
		}
		if (!request.identity.workpieceId || !request.identity.inspectionId ||
			!request.identity.captureId || request.deadline <= Clock::now()) {
			return core::Result<void>::failure(error(
				core::StatusCode::InvalidArgument, "capture requires nonzero ids and a future deadline"));
		}
		if (queue_.size() >= options_.queueCapacity) {
			return core::Result<void>::failure(error(
				core::StatusCode::QueueFull, "event capture queue is full"));
		}
		queue_.push_back(request);
		ready_.notify_all();
		return core::Result<void>::success();
	}

	void requestStop() noexcept {
		{
			std::lock_guard lock(mutex_);
			stopped_ = true;
			accepting_ = false;
		}
		ready_.notify_all();
		device_->requestStop();
	}

	void wait() noexcept {
		std::lock_guard lifecycle(lifecycleMutex_);
		if (worker_.joinable()) {
			worker_.join();
		}
		device_->wait();
	}

	ICameraDevice* cameraDevice() const noexcept { return device_.get(); }
	bool faulted() {
		std::lock_guard lock(mutex_);
		return fault_.has_value();
	}

private:
	using Clock = std::chrono::steady_clock;

	void onFrame(core::Result<vision::Frame> frame) {
		std::lock_guard lock(mutex_);
		if (stopped_ || fault_) {
			logs::error("event source discarded a frame after stop or fault");
			return;
		}
		if (!awaiting_ || response_) {
			fault_ = error(core::StatusCode::DataLoss,
				"unsolicited or duplicate camera response; capture association is unsafe");
			accepting_ = false;
			logs::report(*fault_);
		} else {
			response_.emplace(std::move(frame));
			responseAt_ = Clock::now();
		}
		ready_.notify_all();
	}

	core::Result<vision::Frame> acquire(
		const CaptureRequest& request, std::unique_lock<std::mutex>& lock) {
		if (stopped_) {
			return core::Result<vision::Frame>::failure(error(
				core::StatusCode::Cancelled, "capture cancelled"));
		}
		if (fault_) {
			return core::Result<vision::Frame>::failure(*fault_);
		}
		if (Clock::now() >= request.deadline) {
			return core::Result<vision::Frame>::failure(error(
				core::StatusCode::DeadlineExceeded, "capture expired before triggering"));
		}
		const auto deadline = std::min(request.deadline, Clock::now() + options_.responseTimeout);
		awaiting_ = true;
		response_.reset();
		lock.unlock();
		auto triggered = device_->softwareTrigger();
		lock.lock();
		if (!triggered) {
			fault_ = triggered.status().withContext("event software trigger");
		} else {
			ready_.wait_until(lock, deadline, [this] {
				return stopped_ || fault_ || response_.has_value();
			});
			if (!stopped_ && !fault_ && (!response_ || responseAt_ > deadline)) {
				fault_ = error(core::StatusCode::DeadlineExceeded,
					"camera response timed out; recreate source before further triggers");
			}
		}
		awaiting_ = false;
		if (fault_) {
			accepting_ = false;
			logs::report(*fault_);
			return core::Result<vision::Frame>::failure(*fault_);
		}
		if (stopped_) {
			return core::Result<vision::Frame>::failure(error(
				core::StatusCode::Cancelled, "capture cancelled"));
		}
		auto result = std::move(*response_);
		response_.reset();
		if (!result) {
			fault_ = result.status();
			accepting_ = false;
			logs::report(*fault_);
			return result;
		}
		const auto& frame = result.value();
		auto metadata = frame.metadata();
		metadata.capture = request.identity;
		if (metadata.capturedAt == Clock::time_point{}) {
			metadata.capturedAt = responseAt_;
		}
		return vision::Frame::create(frame.buffer(), frame.width(), frame.height(),
			frame.pixelFormat(), frame.rowStride(), metadata, frame.byteOffset());
	}

	void deliver(const CaptureRequest& request, core::Result<vision::Frame> result) noexcept {
		try {
			callback_(request, std::move(result));
		} catch (const std::exception& exception) {
			logs::error(std::string("capture callback threw: ") + exception.what());
		} catch (...) {
			logs::error("capture callback threw an unknown exception");
		}
	}

	void run() noexcept {
		std::unique_lock lock(mutex_);
		for (;;) {
			ready_.wait(lock, [this] { return stopped_ || fault_ || !queue_.empty(); });
			if (queue_.empty() && (stopped_ || fault_)) {
				break;
			}
			const auto request = queue_.front();
			queue_.pop_front();
			try {
				auto result = acquire(request, lock);
				lock.unlock();
				deliver(request, std::move(result));
			} catch (const std::exception& exception) {
				if (!lock.owns_lock()) {
					lock.lock();
				}
				fault_ = core::Status::error(core::StatusCode::Internal,
					std::string("capture worker threw: ") + exception.what());
				accepting_ = false;
				logs::report(*fault_);
				const auto status = *fault_;
				lock.unlock();
				deliver(request, core::Result<vision::Frame>::failure(status));
			} catch (...) {
				if (!lock.owns_lock()) {
					lock.lock();
				}
				fault_ = error(core::StatusCode::Internal, "capture worker threw an unknown exception");
				accepting_ = false;
				logs::report(*fault_);
				const auto status = *fault_;
				lock.unlock();
				deliver(request, core::Result<vision::Frame>::failure(status));
			}
			lock.lock();
		}
		const auto fault = fault_;
		lock.unlock();
		device_->requestStop();
		if (fault && faultObserver_) {
			try { faultObserver_(*fault); }
			catch (const std::exception& exception) {
				logs::error(std::string("capture fault observer threw: ") + exception.what());
			} catch (...) { logs::error("capture fault observer threw an unknown exception"); }
		}
	}

	std::unique_ptr<ICameraDevice> device_;
	EventTriggerOptions options_;
	CaptureCallback callback_;
	std::function<void(const core::Status&)> faultObserver_;
	std::mutex lifecycleMutex_;
	std::mutex mutex_;
	std::condition_variable ready_;
	std::thread worker_;
	std::deque<CaptureRequest> queue_;
	std::optional<core::Result<vision::Frame>> response_;
	Clock::time_point responseAt_;
	std::optional<core::Status> fault_;
	bool started_ = false;
	bool accepting_ = false;
	bool stopped_ = false;
	bool awaiting_ = false;
};

core::Result<std::unique_ptr<EventTriggerSource>> EventTriggerSource::create(
	std::unique_ptr<ICameraDevice> device, EventTriggerOptions options) {
	using Result = core::Result<std::unique_ptr<EventTriggerSource>>;
	if (!device || !options.queueCapacity || options.responseTimeout.count() <= 0 ||
		options.responseTimeout > std::chrono::hours(24)) {
		return Result::failure(error(core::StatusCode::InvalidArgument,
			"event source requires a device, positive queue capacity and timeout in (0, 24h]"));
	}
	if (!device->capabilities().supportsSoftwareTrigger) {
		return Result::failure(error(core::StatusCode::Unsupported,
			"event source requires software-trigger support"));
	}
	return Result::success(std::unique_ptr<EventTriggerSource>(
		new EventTriggerSource(std::make_unique<Impl>(std::move(device), options))));
}

EventTriggerSource::EventTriggerSource(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
EventTriggerSource::~EventTriggerSource() { requestStop(); wait(); }
core::Result<void> EventTriggerSource::start(CaptureCallback callback,
	std::function<void(const core::Status&)> faultObserver) {
	return impl_->start(std::move(callback), std::move(faultObserver));
}
core::Result<void> EventTriggerSource::request(CaptureRequest request) { return impl_->request(request); }
void EventTriggerSource::requestStop() noexcept { impl_->requestStop(); }
void EventTriggerSource::wait() noexcept { impl_->wait(); }
ICameraDevice* EventTriggerSource::cameraDevice() const noexcept { return impl_->cameraDevice(); }
bool EventTriggerSource::faulted() const { return impl_->faulted(); }

} // namespace visionRuntime::camera
