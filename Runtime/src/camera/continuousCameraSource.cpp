#include "camera/continuousCameraSource.hpp"

#include "core/status.hpp"

#include <mutex>
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
		auto started = device_->startAcquisition({
			.mode = AcquisitionMode::Continuous,
			.frameRate = options_.frameRate,
		}, std::move(callback));
		if (!started) {
			return started;
		}
		started_ = true;
		return core::Result<void>::success();
	}

	void requestStop() noexcept {
		device_->requestStop();
	}

	void wait() noexcept {
		device_->wait();
	}

	[[nodiscard]] bool isRunning() const noexcept {
		return device_->isAcquiring();
	}

	[[nodiscard]] FrameSourceInfo info() const {
		return {
			.outputSpec = device_->outputSpec(),
			.expectedFrameCount = std::nullopt,
			.isFinite = false,
		};
	}

private:
	std::unique_ptr<ICameraDevice> device_;
	ContinuousCameraSourceOptions options_;
	std::mutex mutex_;
	bool started_ = false;
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

} // namespace visionRuntime::camera
