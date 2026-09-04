#include "camera/timedTriggerSource.hpp"

#include "core/tensorBuffer.hpp"
#include "memory/cpuAllocator.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace {

using namespace std::chrono_literals;

class FakeTriggerCameraDevice final : public visionRuntime::camera::ICameraDevice {
public:
	explicit FakeTriggerCameraDevice(bool supportsSoftwareTrigger = true) {
		capabilities_.supportsSoftwareTrigger = supportsSoftwareTrigger;
	}

	visionRuntime::core::Result<void> startAcquisition(
		visionRuntime::camera::CameraAcquisitionOptions options,
		visionRuntime::camera::FrameCallback callback) override {
		std::scoped_lock lock(mutex_);
		++startCount;
		lastOptions = options;
		if (failNextStart) {
			failNextStart = false;
			return visionRuntime::core::Result<void>::failure(
				visionRuntime::core::Status::error(
					visionRuntime::core::StatusCode::Unavailable,
					"fake start failure"));
		}
		callback_ = std::move(callback);
		acquiring = true;
		return visionRuntime::core::Result<void>::success();
	}

	void requestStop() noexcept override {
		std::scoped_lock lock(mutex_);
		++stopCount;
		acquiring = false;
		ready_.notify_all();
	}

	void wait() noexcept override {
		std::scoped_lock lock(mutex_);
		++waitCount;
		acquiring = false;
		callback_ = {};
	}

	[[nodiscard]] bool isAcquiring() const noexcept override {
		std::scoped_lock lock(mutex_);
		return acquiring;
	}

	visionRuntime::core::Result<void> softwareTrigger() override {
		std::scoped_lock lock(mutex_);
		++triggerCount;
		ready_.notify_all();
		if (failTrigger) {
			return visionRuntime::core::Result<void>::failure(
				visionRuntime::core::Status::error(
					visionRuntime::core::StatusCode::BackendError,
					"fake trigger failure"));
		}
		return visionRuntime::core::Result<void>::success();
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

	bool waitForTriggers(std::size_t count, std::chrono::milliseconds timeout) {
		std::unique_lock lock(mutex_);
		return ready_.wait_for(lock, timeout,
			[&] { return triggerCount >= count; });
	}

	void emitFrame() {
		visionRuntime::camera::FrameCallback callback;
		{
			std::scoped_lock lock(mutex_);
			callback = callback_;
		}
		auto buffer = visionRuntime::memory::CpuAllocator{}.allocate(1);
		auto frame = visionRuntime::vision::Frame::create(
			std::move(buffer).value(), 1, 1,
			visionRuntime::vision::PixelFormat::Gray8);
		callback(std::move(frame));
	}

	void emitError(visionRuntime::core::StatusCode code) {
		visionRuntime::camera::FrameCallback callback;
		{
			std::scoped_lock lock(mutex_);
			callback = callback_;
		}
		callback(visionRuntime::core::Result<visionRuntime::vision::Frame>::failure(
			visionRuntime::core::Status::error(code, "fake device error")));
	}

	visionRuntime::camera::CameraAcquisitionOptions lastOptions;
	std::size_t startCount = 0;
	std::size_t stopCount = 0;
	std::size_t waitCount = 0;
	std::size_t triggerCount = 0;
	bool failNextStart = false;
	bool failTrigger = false;
	bool acquiring = false;

private:
	mutable std::mutex mutex_;
	std::condition_variable ready_;
	visionRuntime::camera::FrameCallback callback_;
	visionRuntime::camera::CameraDeviceInfo deviceInfo_;
	visionRuntime::camera::CameraCapabilities capabilities_;
};

} // namespace

TEST(TimedTriggerSourceTest, ValidatesDeviceOptionsAndCapability) {
	using namespace visionRuntime;

	auto missing = camera::TimedTriggerSource::create(nullptr);
	ASSERT_FALSE(missing);
	EXPECT_EQ(missing.status().code(), core::StatusCode::InvalidArgument);

	auto invalid = camera::TimedTriggerSource::create(
		std::make_unique<FakeTriggerCameraDevice>(),
		{.triggerInterval = -1ms, .responseTimeout = 10ms});
	ASSERT_FALSE(invalid);
	EXPECT_EQ(invalid.status().code(), core::StatusCode::InvalidArgument);

	auto unsupported = camera::TimedTriggerSource::create(
		std::make_unique<FakeTriggerCameraDevice>(false));
	ASSERT_FALSE(unsupported);
	EXPECT_EQ(unsupported.status().code(), core::StatusCode::Unsupported);
}

TEST(TimedTriggerSourceTest, UsesInputFrameIntervalAndWaitsForCallbackCompletion) {
	using namespace visionRuntime;

	auto device = std::make_unique<FakeTriggerCameraDevice>();
	auto* devicePointer = device.get();
	auto sourceResult = camera::TimedTriggerSource::create(
		std::move(device), {.triggerInterval = 200ms, .responseTimeout = 500ms});
	ASSERT_TRUE(sourceResult);
	auto source = std::move(sourceResult).value();
	const auto info = source->info();
	EXPECT_FALSE(info.isFinite);
	EXPECT_FALSE(info.expectedFrameCount);
	EXPECT_EQ(info.outputSpec.width, 1U);

	auto emptyCallback = source->start({});
	ASSERT_FALSE(emptyCallback);
	EXPECT_EQ(emptyCallback.status().code(), core::StatusCode::InvalidArgument);

	std::mutex callbackMutex;
	std::condition_variable callbackReady;
	bool callbackEntered = false;
	bool releaseCallback = false;
	ASSERT_TRUE(source->start([&](core::Result<vision::Frame> frame) {
		ASSERT_TRUE(frame) << frame.status().toString();
		std::unique_lock lock(callbackMutex);
		callbackEntered = true;
		callbackReady.notify_all();
		callbackReady.wait(lock, [&] { return releaseCallback; });
	}));
	auto restarted = source->start([](core::Result<vision::Frame>) {});
	ASSERT_FALSE(restarted);
	EXPECT_EQ(restarted.status().code(), core::StatusCode::InvalidState);
	ASSERT_TRUE(devicePointer->waitForTriggers(1, 500ms));
	EXPECT_EQ(devicePointer->lastOptions.mode, camera::AcquisitionMode::SoftwareTrigger);
	EXPECT_FALSE(devicePointer->lastOptions.frameRate);

	std::jthread emission([&] { devicePointer->emitFrame(); });
	{
		std::unique_lock lock(callbackMutex);
		ASSERT_TRUE(callbackReady.wait_for(lock, 500ms,
			[&] { return callbackEntered; }));
	}
	EXPECT_FALSE(devicePointer->waitForTriggers(2, 250ms));
	{
		std::scoped_lock lock(callbackMutex);
		releaseCallback = true;
	}
	callbackReady.notify_all();
	emission.join();
	EXPECT_TRUE(devicePointer->waitForTriggers(2, 100ms));

	source->requestStop();
	source->requestStop();
	source->wait();
	source->wait();
	EXPECT_FALSE(source->isRunning());
}

TEST(TimedTriggerSourceTest, ForwardsDeviceErrorDuringFrameInterval) {
	using namespace visionRuntime;

	auto device = std::make_unique<FakeTriggerCameraDevice>();
	auto* devicePointer = device.get();
	auto sourceResult = camera::TimedTriggerSource::create(
		std::move(device), {.triggerInterval = 5s, .responseTimeout = 500ms});
	ASSERT_TRUE(sourceResult);
	auto source = std::move(sourceResult).value();

	std::mutex mutex;
	std::condition_variable ready;
	std::size_t callbackCount = 0;
	core::StatusCode errorCode = core::StatusCode::Ok;
	ASSERT_TRUE(source->start([&](core::Result<vision::Frame> frame) {
		std::scoped_lock lock(mutex);
		++callbackCount;
		if (!frame) {
			errorCode = frame.status().code();
		}
		ready.notify_all();
	}));
	ASSERT_TRUE(devicePointer->waitForTriggers(1, 500ms));
	devicePointer->emitFrame();
	{
		std::unique_lock lock(mutex);
		ASSERT_TRUE(ready.wait_for(lock, 500ms,
			[&] { return callbackCount == 1; }));
	}

	devicePointer->emitError(core::StatusCode::DataLoss);
	{
		std::unique_lock lock(mutex);
		ASSERT_TRUE(ready.wait_for(lock, 500ms,
			[&] { return callbackCount == 2; }));
	}
	source->wait();
	EXPECT_EQ(errorCode, core::StatusCode::DataLoss);
	EXPECT_EQ(devicePointer->triggerCount, 1U);
	EXPECT_FALSE(source->isRunning());
}

TEST(TimedTriggerSourceTest, StopInterruptsResponseWait) {
	using namespace visionRuntime;

	auto device = std::make_unique<FakeTriggerCameraDevice>();
	auto* devicePointer = device.get();
	auto sourceResult = camera::TimedTriggerSource::create(
		std::move(device), {.triggerInterval = 0ms, .responseTimeout = 5s});
	ASSERT_TRUE(sourceResult);
	auto source = std::move(sourceResult).value();
	ASSERT_TRUE(source->start([](core::Result<vision::Frame>) {}));
	ASSERT_TRUE(devicePointer->waitForTriggers(1, 500ms));

	const auto stopStarted = std::chrono::steady_clock::now();
	source->requestStop();
	source->wait();
	const auto stopDuration = std::chrono::steady_clock::now() - stopStarted;
	EXPECT_LT(stopDuration, 500ms);
}

TEST(TimedTriggerSourceTest, ReportsResponseTimeoutOnceAndStops) {
	using namespace visionRuntime;

	auto device = std::make_unique<FakeTriggerCameraDevice>();
	auto* devicePointer = device.get();
	auto sourceResult = camera::TimedTriggerSource::create(
		std::move(device), {.triggerInterval = 0ms, .responseTimeout = 30ms});
	ASSERT_TRUE(sourceResult);
	auto source = std::move(sourceResult).value();

	std::mutex mutex;
	std::condition_variable ready;
	std::size_t callbackCount = 0;
	core::StatusCode errorCode = core::StatusCode::Ok;
	ASSERT_TRUE(source->start([&](core::Result<vision::Frame> frame) {
		std::scoped_lock lock(mutex);
		++callbackCount;
		if (!frame) {
			errorCode = frame.status().code();
		}
		ready.notify_all();
	}));
	{
		std::unique_lock lock(mutex);
		ASSERT_TRUE(ready.wait_for(lock, 500ms,
			[&] { return callbackCount == 1; }));
	}
	source->wait();
	EXPECT_EQ(callbackCount, 1U);
	EXPECT_EQ(errorCode, core::StatusCode::DeadlineExceeded);
	EXPECT_EQ(devicePointer->triggerCount, 1U);
	EXPECT_FALSE(source->isRunning());
}

TEST(TimedTriggerSourceTest, ForwardsTriggerAndDeviceErrorsOnce) {
	using namespace visionRuntime;

	{
		auto device = std::make_unique<FakeTriggerCameraDevice>();
		device->failTrigger = true;
		auto sourceResult = camera::TimedTriggerSource::create(std::move(device));
		ASSERT_TRUE(sourceResult);
		auto source = std::move(sourceResult).value();
		std::mutex mutex;
		std::condition_variable ready;
		std::size_t count = 0;
		core::StatusCode code = core::StatusCode::Ok;
		ASSERT_TRUE(source->start([&](core::Result<vision::Frame> frame) {
			std::scoped_lock lock(mutex);
			++count;
			code = frame.status().code();
			ready.notify_all();
		}));
		{
			std::unique_lock lock(mutex);
			ASSERT_TRUE(ready.wait_for(lock, 500ms, [&] { return count == 1; }));
		}
		source->wait();
		EXPECT_EQ(code, core::StatusCode::BackendError);
		EXPECT_EQ(count, 1U);
	}

	{
		auto device = std::make_unique<FakeTriggerCameraDevice>();
		auto* devicePointer = device.get();
		auto sourceResult = camera::TimedTriggerSource::create(std::move(device));
		ASSERT_TRUE(sourceResult);
		auto source = std::move(sourceResult).value();
		std::size_t count = 0;
		core::StatusCode code = core::StatusCode::Ok;
		ASSERT_TRUE(source->start([&](core::Result<vision::Frame> frame) {
			++count;
			code = frame.status().code();
		}));
		ASSERT_TRUE(devicePointer->waitForTriggers(1, 500ms));
		devicePointer->emitError(core::StatusCode::DataLoss);
		source->wait();
		EXPECT_EQ(code, core::StatusCode::DataLoss);
		EXPECT_EQ(count, 1U);
	}
}

TEST(TimedTriggerSourceTest, AllowsRetryAfterDeviceStartFailure) {
	using namespace visionRuntime;

	auto device = std::make_unique<FakeTriggerCameraDevice>();
	auto* devicePointer = device.get();
	devicePointer->failNextStart = true;
	auto sourceResult = camera::TimedTriggerSource::create(std::move(device));
	ASSERT_TRUE(sourceResult);
	auto source = std::move(sourceResult).value();

	auto failed = source->start([](core::Result<vision::Frame>) {});
	ASSERT_FALSE(failed);
	EXPECT_EQ(failed.status().code(), core::StatusCode::Unavailable);
	EXPECT_TRUE(source->start([](core::Result<vision::Frame>) {}));
	EXPECT_EQ(devicePointer->startCount, 2U);
	source->requestStop();
	source->wait();
}
