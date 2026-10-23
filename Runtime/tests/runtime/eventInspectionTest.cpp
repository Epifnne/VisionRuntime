#include "runtime/eventInspectionSession.hpp"
#include "memory/cpuAllocator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {
using namespace visionRuntime;
using namespace std::chrono_literals;

class TestCamera final : public camera::ICameraDevice {
public:
	explicit TestCamera(bool automatic = true) : automatic_(automatic) {
		capabilities_.supportsSoftwareTrigger = true;
	}

	core::Result<void> startAcquisition(
		camera::CameraAcquisitionOptions options, camera::FrameCallback callback) override {
		std::lock_guard lock(mutex_);
		if (failStart) {
			return core::Result<void>::failure(core::Status::error(
				core::StatusCode::Unavailable, "test camera failed to start"));
		}
		mode = options.mode;
		callback_ = std::move(callback);
		running_ = true;
		return core::Result<void>::success();
	}
	void requestStop() noexcept override {
		std::lock_guard lock(mutex_);
		running_ = false;
		ready_.notify_all();
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
	core::Result<void> softwareTrigger() override {
		{
			std::lock_guard lock(mutex_);
			if (!running_) {
				return core::Result<void>::failure(core::Status::error(
					core::StatusCode::InvalidState, "test camera is stopped"));
			}
			++triggers_;
			ready_.notify_all();
		}
		if (automatic_) {
			emit();
			if (duplicateResponse) {
				emit();
			}
		}
		return core::Result<void>::success();
	}
	void emit(std::optional<std::uint64_t> explicitSequence = {}) {
		camera::FrameCallback callback;
		std::uint64_t sequence;
		{
			std::lock_guard lock(mutex_);
			if (!callback_) {
				return;
			}
			callback = callback_;
			sequence = explicitSequence.value_or(triggers_);
			++callbacks_;
		}
		auto buffer = memory::CpuAllocator{}.allocate(4);
		ASSERT_TRUE(buffer);
		callback(vision::Frame::create(std::move(buffer).value(), 1, 1,
			vision::PixelFormat::Gray8, 1, {.sequenceNumber = sequence}, 2));
		{
			std::lock_guard lock(mutex_);
			--callbacks_;
			ready_.notify_all();
		}
	}
	bool awaitTriggers(std::size_t count) {
		std::unique_lock lock(mutex_);
		return ready_.wait_for(lock, 1s, [&] { return triggers_ >= count; });
	}
	std::size_t triggerCount() const {
		std::lock_guard lock(mutex_);
		return triggers_;
	}
	const camera::CameraDeviceInfo& deviceInfo() const noexcept override { return info_; }
	const camera::CameraCapabilities& capabilities() const noexcept override { return capabilities_; }
	vision::FrameSpec outputSpec() const override {
		return {{vision::PixelFormat::Gray8}, 1, 1, core::Device::cpu()};
	}
	bool failStart = false;
	bool duplicateResponse = false;
	camera::AcquisitionMode mode = camera::AcquisitionMode::Continuous;

private:
	bool automatic_;
	mutable std::mutex mutex_;
	std::condition_variable ready_;
	camera::FrameCallback callback_;
	std::size_t triggers_ = 0;
	std::size_t callbacks_ = 0;
	bool running_ = false;
	camera::CameraDeviceInfo info_;
	camera::CameraCapabilities capabilities_;
};

camera::CaptureRequest request(std::uint64_t captureId, std::chrono::milliseconds timeout = 1s) {
	return {{11, 22, captureId, 3}, std::chrono::steady_clock::now() + timeout};
}

struct Captures {
	std::mutex mutex;
	std::condition_variable ready;
	std::vector<vision::CaptureIdentity> identities;
	std::vector<core::StatusCode> codes;
	bool waitFor(std::size_t count) {
		std::unique_lock lock(mutex);
		return ready.wait_for(lock, 2s, [&] { return codes.size() >= count; });
	}
	camera::CaptureCallback callback() {
		return [this](const auto& request, core::Result<vision::Frame> frame) {
			std::lock_guard lock(mutex);
			identities.push_back(request.identity);
			codes.push_back(frame.status().code());
			if (frame) {
				EXPECT_EQ(frame->metadata().capture, request.identity);
				EXPECT_EQ(frame->byteOffset(), 2U);
				pipeline::PipelinePacket packet(std::move(frame).value(), {}, 3);
				packet.completeImagePreparation();
				EXPECT_EQ(packet.identity().metadata.capture, request.identity);
			}
			ready.notify_all();
		};
	}
};

struct Results {
	std::mutex mutex;
	std::condition_variable ready;
	std::vector<runtime::InspectionResult> values;
	void observe(const runtime::InspectionResult& result) {
		std::lock_guard lock(mutex);
		values.push_back(result);
		ready.notify_all();
	}
	bool waitFor(std::size_t count) {
		std::unique_lock lock(mutex);
		return ready.wait_for(lock, 2s, [&] { return values.size() >= count; });
	}
};

template<typename T>
class ConstantPipeline final : public pipeline::IVisionPipeline<T> {
public:
	explicit ConstantPipeline(T value, std::chrono::milliseconds delay = {})
		: value_(value), delay_(delay) {}
	core::Result<T> run(pipeline::PipelinePacket packet) override {
		EXPECT_TRUE(packet.identity().metadata.capture);
		std::this_thread::sleep_for(delay_);
		return core::Result<T>::success(value_);
	}
private:
	T value_;
	std::chrono::milliseconds delay_;
};

struct Gate {
	std::mutex mutex;
	std::condition_variable ready;
	bool entered = false;
	bool released = false;
	bool awaitEntry() {
		std::unique_lock lock(mutex);
		return ready.wait_for(lock, 1s, [this] { return entered; });
	}
	void release() {
		std::lock_guard lock(mutex);
		released = true;
		ready.notify_all();
	}
};

struct ReleaseGate {
	Gate& gate;
	~ReleaseGate() { gate.release(); }
};

class GatedPipeline final : public pipeline::IVisionPipeline<bool> {
public:
	explicit GatedPipeline(Gate& gate) : gate_(gate) {}
	core::Result<bool> run(pipeline::PipelinePacket) override {
		std::unique_lock lock(gate_.mutex);
		gate_.entered = true;
		gate_.ready.notify_all();
		gate_.ready.wait(lock, [this] { return gate_.released; });
		return core::Result<bool>::success(true);
	}
private:
	Gate& gate_;
};

core::Result<void> addRoute(runtime::EventInspectionSession& session, std::uint32_t view,
	bool ok = true, std::chrono::milliseconds delay = {}) {
	auto source = camera::EventTriggerSource::create(std::make_unique<TestCamera>());
	if (!source) {
		return core::Result<void>::failure(source.status());
	}
	return session.addRoute<bool>(view, std::move(source).value(),
		std::make_unique<ConstantPipeline<bool>>(ok, delay),
		[](bool value) { return core::Result<runtime::ViewDecision>::success(
			value ? runtime::ViewDecision::Ok : runtime::ViewDecision::Ng); });
}

TEST(EventTriggerSourceTest, ValidatesConstructionAndDoesNotTriggerUntilRequested) {
	EXPECT_FALSE(camera::EventTriggerSource::create(nullptr));
	EXPECT_FALSE(camera::EventTriggerSource::create(std::make_unique<TestCamera>(),
		{.queueCapacity = 0}));
	auto device = std::make_unique<TestCamera>();
	auto* raw = device.get();
	auto made = camera::EventTriggerSource::create(std::move(device));
	ASSERT_TRUE(made);
	auto source = std::move(made).value();
	EXPECT_FALSE(source->request(request(1)));
	EXPECT_FALSE(source->start({}));
	Captures captures;
	ASSERT_TRUE(source->start(captures.callback()));
	EXPECT_EQ(raw->mode, camera::AcquisitionMode::SoftwareTrigger);
	EXPECT_EQ(raw->triggerCount(), 0U);
	ASSERT_TRUE(source->request(request(1)));
	ASSERT_TRUE(captures.waitFor(1));
	source->requestStop();
	source->wait();
	EXPECT_EQ(captures.codes, std::vector{core::StatusCode::Ok});
	EXPECT_FALSE(source->start(captures.callback()));
}

TEST(EventTriggerSourceTest, BoundedQueueCancelsEveryAcceptedRequestExactlyOnce) {
	auto device = std::make_unique<TestCamera>(false);
	auto* raw = device.get();
	auto made = camera::EventTriggerSource::create(std::move(device), {.queueCapacity = 1});
	ASSERT_TRUE(made);
	auto source = std::move(made).value();
	Captures captures;
	ASSERT_TRUE(source->start(captures.callback()));
	ASSERT_TRUE(source->request(request(1)));
	ASSERT_TRUE(raw->awaitTriggers(1));
	ASSERT_TRUE(source->request(request(2)));
	auto overflow = source->request(request(3));
	ASSERT_FALSE(overflow);
	EXPECT_EQ(overflow.status().code(), core::StatusCode::QueueFull);
	source->requestStop();
	source->wait();
	ASSERT_EQ(captures.codes.size(), 2U);
	EXPECT_EQ(captures.codes[0], core::StatusCode::Cancelled);
	EXPECT_EQ(captures.codes[1], core::StatusCode::Cancelled);
	EXPECT_EQ(raw->triggerCount(), 1U);
}

TEST(EventTriggerSourceTest, TimeoutLatchesFaultAndLateFrameCannotSatisfyAnotherCapture) {
	auto device = std::make_unique<TestCamera>(false);
	auto* raw = device.get();
	auto made = camera::EventTriggerSource::create(std::move(device),
		{.responseTimeout = 30ms});
	ASSERT_TRUE(made);
	auto source = std::move(made).value();
	Captures captures;
	ASSERT_TRUE(source->start(captures.callback()));
	ASSERT_TRUE(source->request(request(1)));
	ASSERT_TRUE(raw->awaitTriggers(1));
	ASSERT_TRUE(source->request(request(2)));
	ASSERT_TRUE(captures.waitFor(2));
	raw->emit();
	EXPECT_FALSE(source->request(request(3)));
	source->requestStop();
	source->wait();
	ASSERT_EQ(captures.codes.size(), 2U);
	EXPECT_EQ(captures.codes[0], core::StatusCode::DeadlineExceeded);
	EXPECT_EQ(captures.codes[1], core::StatusCode::DeadlineExceeded);
	EXPECT_EQ(raw->triggerCount(), 1U);
}

TEST(EventTriggerSourceTest, DuplicateDeviceResponseFaultsInsteadOfBeingReused) {
	auto device = std::make_unique<TestCamera>();
	device->duplicateResponse = true;
	auto made = camera::EventTriggerSource::create(std::move(device));
	ASSERT_TRUE(made);
	auto source = std::move(made).value();
	Captures captures;
	ASSERT_TRUE(source->start(captures.callback()));
	ASSERT_TRUE(source->request(request(1)));
	ASSERT_TRUE(captures.waitFor(1));
	EXPECT_FALSE(source->request(request(2)));
	source->requestStop();
	source->wait();
	ASSERT_EQ(captures.codes.size(), 1U);
	EXPECT_EQ(captures.codes[0], core::StatusCode::DataLoss);
}

TEST(EventTriggerSourceTest, ExpiredQueuedCaptureIsNotTriggeredAndDoesNotFaultHealthyCamera) {
	auto device = std::make_unique<TestCamera>(false);
	auto* raw = device.get();
	auto made = camera::EventTriggerSource::create(std::move(device));
	ASSERT_TRUE(made);
	auto source = std::move(made).value();
	Captures captures;
	ASSERT_TRUE(source->start(captures.callback()));
	ASSERT_TRUE(source->request(request(1)));
	ASSERT_TRUE(raw->awaitTriggers(1));
	ASSERT_TRUE(source->request(request(2, 20ms)));
	std::this_thread::sleep_for(40ms);
	raw->emit();
	ASSERT_TRUE(captures.waitFor(2));
	ASSERT_TRUE(source->request(request(3)));
	ASSERT_TRUE(raw->awaitTriggers(2));
	raw->emit();
	ASSERT_TRUE(captures.waitFor(3));
	source->requestStop();
	source->wait();
	EXPECT_EQ(captures.codes, (std::vector{core::StatusCode::Ok,
		core::StatusCode::DeadlineExceeded, core::StatusCode::Ok}));
	EXPECT_EQ(raw->triggerCount(), 2U);
}

TEST(EventInspectionTest, RoutesDifferentResultTypesAndWaitsForAllViewsBeforeNg) {
	struct Measurement { double length; };
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	ASSERT_TRUE(addRoute(session, 0));
	auto source = camera::EventTriggerSource::create(std::make_unique<TestCamera>());
	ASSERT_TRUE(source);
	std::atomic_size_t typedResults = 0;
	ASSERT_TRUE(session.addRoute<Measurement>(1, std::move(source).value(),
		std::make_unique<ConstantPipeline<Measurement>>(Measurement{12.5}),
		[](const Measurement& value) { return core::Result<runtime::ViewDecision>::success(
			value.length < 10 ? runtime::ViewDecision::Ok : runtime::ViewDecision::Ng); },
		[&](const auto& identity, const auto& result) {
			EXPECT_EQ(identity.sourceId, 1U);
			EXPECT_EQ(identity.metadata.capture->workpieceId, 17U);
			EXPECT_DOUBLE_EQ(result->length, 12.5);
			++typedResults;
		}));
	ASSERT_TRUE(session.start());
	auto inspection = session.beginInspection(17, {0, 1}, 1s);
	ASSERT_TRUE(inspection);
	auto first = session.requestCapture(inspection.value(), 1);
	ASSERT_TRUE(first);
	const auto limit = std::chrono::steady_clock::now() + 1s;
	while (typedResults == 0 && std::chrono::steady_clock::now() < limit) {
		std::this_thread::yield();
	}
	ASSERT_EQ(typedResults, 1U);
	{
		std::lock_guard lock(results.mutex);
		EXPECT_TRUE(results.values.empty());
	}
	ASSERT_TRUE(session.requestCapture(inspection.value(), 0));
	ASSERT_TRUE(results.waitFor(1));
	session.requestStop();
	session.wait();
	ASSERT_EQ(results.values.size(), 1U);
	EXPECT_EQ(results.values[0].decision, runtime::InspectionDecision::Ng);
	EXPECT_EQ(results.values[0].inspectionId, inspection.value());
	ASSERT_EQ(results.values[0].views.size(), 2U);
	EXPECT_EQ(results.values[0].views[1].capture, first.value());
	EXPECT_EQ(results.values[0].views[1].frame->metadata.capture, first.value());
}

TEST(EventInspectionTest, RejectsInvalidDuplicateAndUnplannedRequestsWithoutExtraTriggers) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); },
		{.maxInspectionsInFlight = 1});
	ASSERT_TRUE(addRoute(session, 0));
	EXPECT_FALSE(session.beginInspection(1, {0}, 1s));
	ASSERT_TRUE(session.start());
	EXPECT_FALSE(session.beginInspection(0, {0}, 1s));
	EXPECT_FALSE(session.beginInspection(1, {0, 0}, 1s));
	EXPECT_FALSE(session.beginInspection(1, {2}, 1s));
	EXPECT_FALSE(session.beginInspection(1, {0}, 0ms));
	auto inspection = session.beginInspection(1, {0}, 1s);
	ASSERT_TRUE(inspection);
	EXPECT_FALSE(session.beginInspection(2, {0}, 1s));
	EXPECT_FALSE(session.requestCapture(inspection.value(), 2));
	ASSERT_TRUE(session.requestCapture(inspection.value(), 0));
	EXPECT_FALSE(session.requestCapture(inspection.value(), 0));
	ASSERT_TRUE(results.waitFor(1));
	EXPECT_FALSE(session.requestCapture(inspection.value(), 0));
	session.requestStop();
	session.wait();
	ASSERT_EQ(results.values.size(), 1U);
	EXPECT_EQ(results.values[0].decision, runtime::InspectionDecision::Ok);
}

TEST(EventInspectionTest, MissingViewTimesOutEvenWithoutAnotherIncomingEvent) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	ASSERT_TRUE(addRoute(session, 0, false));
	ASSERT_TRUE(addRoute(session, 1));
	ASSERT_TRUE(session.start());
	auto inspection = session.beginInspection(1, {0, 1}, 60ms);
	ASSERT_TRUE(inspection);
	ASSERT_TRUE(session.requestCapture(inspection.value(), 0));
	ASSERT_TRUE(results.waitFor(1));
	session.requestStop();
	session.wait();
	ASSERT_EQ(results.values.size(), 1U);
	EXPECT_EQ(results.values[0].decision, runtime::InspectionDecision::Fault);
	EXPECT_EQ(results.values[0].views[1].status.code(), core::StatusCode::DeadlineExceeded);
	EXPECT_FALSE(results.values[0].views[1].capture);
	EXPECT_FALSE(session.requestCapture(inspection.value(), 1));
}

TEST(EventInspectionTest, SlowPipelineCannotTurnAnExpiredInspectionIntoOk) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	ASSERT_TRUE(addRoute(session, 0, true, 120ms));
	ASSERT_TRUE(session.start());
	auto first = session.beginInspection(1, {0}, 50ms);
	ASSERT_TRUE(first);
	ASSERT_TRUE(session.requestCapture(first.value(), 0));
	ASSERT_TRUE(results.waitFor(1));
	auto next = session.beginInspection(2, {0}, 1s);
	ASSERT_TRUE(next);
	ASSERT_TRUE(session.requestCapture(next.value(), 0));
	ASSERT_TRUE(results.waitFor(2));
	session.requestStop();
	session.wait();
	ASSERT_EQ(results.values.size(), 2U);
	EXPECT_EQ(results.values[0].workpieceId, 1U);
	EXPECT_EQ(results.values[0].decision, runtime::InspectionDecision::Fault);
	EXPECT_EQ(results.values[1].workpieceId, 2U);
	EXPECT_EQ(results.values[1].decision, runtime::InspectionDecision::Ok);
}

TEST(EventInspectionTest, OverlappingWorkpiecesAndOutOfOrderViewsRemainIsolated) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	ASSERT_TRUE(addRoute(session, 0));
	ASSERT_TRUE(addRoute(session, 1, false));
	ASSERT_TRUE(session.start());
	auto a = session.beginInspection(101, {0, 1}, 1s);
	auto b = session.beginInspection(102, {0, 1}, 1s);
	ASSERT_TRUE(a);
	ASSERT_TRUE(b);
	ASSERT_TRUE(session.requestCapture(a.value(), 1));
	ASSERT_TRUE(session.requestCapture(b.value(), 0));
	ASSERT_TRUE(session.requestCapture(b.value(), 1));
	ASSERT_TRUE(results.waitFor(1));
	ASSERT_TRUE(session.requestCapture(a.value(), 0));
	ASSERT_TRUE(results.waitFor(2));
	session.requestStop();
	session.wait();
	ASSERT_EQ(results.values.size(), 2U);
	EXPECT_EQ(results.values[0].workpieceId, 102U);
	EXPECT_EQ(results.values[1].workpieceId, 101U);
	for (const auto& result : results.values) {
		EXPECT_EQ(result.decision, runtime::InspectionDecision::Ng);
		for (const auto& view : result.views) {
			ASSERT_TRUE(view.capture);
			EXPECT_EQ(view.capture->workpieceId, result.workpieceId);
			EXPECT_EQ(view.capture->inspectionId, result.inspectionId);
		}
	}
}

TEST(EventInspectionTest, StopFaultsUntriggeredViewsAndCannotRestart) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	ASSERT_TRUE(addRoute(session, 0));
	ASSERT_TRUE(session.start());
	ASSERT_TRUE(session.beginInspection(1, {0}, 1s));
	session.requestStop();
	session.wait();
	ASSERT_EQ(results.values.size(), 1U);
	EXPECT_EQ(results.values[0].decision, runtime::InspectionDecision::Fault);
	EXPECT_EQ(results.values[0].views[0].status.code(), core::StatusCode::Cancelled);
	EXPECT_FALSE(session.start());
	EXPECT_FALSE(addRoute(session, 2));
}

TEST(EventInspectionTest, StopDuringPipelineExecutionCannotPublishOk) {
	Results results;
	Gate gate;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	ReleaseGate release{gate};
	auto source = camera::EventTriggerSource::create(std::make_unique<TestCamera>());
	ASSERT_TRUE(source);
	ASSERT_TRUE(session.addRoute<bool>(0, std::move(source).value(),
		std::make_unique<GatedPipeline>(gate),
		[](bool) { return core::Result<runtime::ViewDecision>::success(runtime::ViewDecision::Ok); }));
	ASSERT_TRUE(session.start());
	auto inspection = session.beginInspection(1, {0}, 2s);
	ASSERT_TRUE(inspection);
	ASSERT_TRUE(session.requestCapture(inspection.value(), 0));
	ASSERT_TRUE(gate.awaitEntry());
	session.requestStop();
	gate.release();
	session.wait();
	ASSERT_EQ(results.values.size(), 1U);
	EXPECT_EQ(results.values[0].decision, runtime::InspectionDecision::Fault);
	EXPECT_EQ(results.values[0].views[0].status.code(), core::StatusCode::Cancelled);
}

TEST(EventInspectionTest, AcquisitionQueueRejectionBecomesCorrelatedFault) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	auto device = std::make_unique<TestCamera>(false);
	auto* raw = device.get();
	auto source = camera::EventTriggerSource::create(std::move(device), {.queueCapacity = 1});
	ASSERT_TRUE(source);
	ASSERT_TRUE(session.addRoute<bool>(0, std::move(source).value(),
		std::make_unique<ConstantPipeline<bool>>(true),
		[](bool) { return core::Result<runtime::ViewDecision>::success(runtime::ViewDecision::Ok); }));
	ASSERT_TRUE(session.start());
	auto first = session.beginInspection(1, {0}, 2s);
	auto second = session.beginInspection(2, {0}, 2s);
	auto third = session.beginInspection(3, {0}, 2s);
	ASSERT_TRUE(first);
	ASSERT_TRUE(second);
	ASSERT_TRUE(third);
	ASSERT_TRUE(session.requestCapture(first.value(), 0));
	ASSERT_TRUE(raw->awaitTriggers(1));
	ASSERT_TRUE(session.requestCapture(second.value(), 0));
	auto rejected = session.requestCapture(third.value(), 0);
	ASSERT_FALSE(rejected);
	EXPECT_EQ(rejected.status().code(), core::StatusCode::QueueFull);
	ASSERT_TRUE(results.waitFor(1));
	session.requestStop();
	session.wait();
	ASSERT_EQ(results.values.size(), 3U);
	EXPECT_EQ(results.values[0].workpieceId, 3U);
	EXPECT_EQ(results.values[0].decision, runtime::InspectionDecision::Fault);
	EXPECT_EQ(results.values[0].views[0].status.code(), core::StatusCode::QueueFull);
}

TEST(EventInspectionTest, BusyPipelineRejectsWithoutBlockingAcquisitionThread) {
	Results results;
	Gate gate;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	ReleaseGate release{gate};
	auto source = camera::EventTriggerSource::create(std::make_unique<TestCamera>());
	ASSERT_TRUE(source);
	ASSERT_TRUE(session.addRoute<bool>(0, std::move(source).value(),
		std::make_unique<GatedPipeline>(gate),
		[](bool) { return core::Result<runtime::ViewDecision>::success(runtime::ViewDecision::Ok); },
		{}, {.queueCapacity = 1}));
	ASSERT_TRUE(session.start());
	auto first = session.beginInspection(1, {0}, 2s);
	auto second = session.beginInspection(2, {0}, 2s);
	auto third = session.beginInspection(3, {0}, 2s);
	ASSERT_TRUE(first);
	ASSERT_TRUE(second);
	ASSERT_TRUE(third);
	ASSERT_TRUE(session.requestCapture(first.value(), 0));
	ASSERT_TRUE(gate.awaitEntry());
	ASSERT_TRUE(session.requestCapture(second.value(), 0));
	ASSERT_TRUE(session.requestCapture(third.value(), 0));
	ASSERT_TRUE(results.waitFor(1));
	{
		std::lock_guard lock(results.mutex);
		ASSERT_EQ(results.values[0].workpieceId, 3U);
		EXPECT_EQ(results.values[0].decision, runtime::InspectionDecision::Fault);
		EXPECT_EQ(results.values[0].views[0].status.code(), core::StatusCode::QueueFull);
	}
	gate.release();
	ASSERT_TRUE(results.waitFor(3));
	session.requestStop();
	session.wait();
	EXPECT_EQ(results.values[1].decision, runtime::InspectionDecision::Ok);
	EXPECT_EQ(results.values[2].decision, runtime::InspectionDecision::Ok);
}

TEST(EventInspectionTest, CaptureDeadlineCanBeEarlierThanWholeInspectionDeadline) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	auto source = camera::EventTriggerSource::create(std::make_unique<TestCamera>(false));
	ASSERT_TRUE(source);
	ASSERT_TRUE(session.addRoute<bool>(0, std::move(source).value(),
		std::make_unique<ConstantPipeline<bool>>(true),
		[](bool) { return core::Result<runtime::ViewDecision>::success(runtime::ViewDecision::Ok); }));
	ASSERT_TRUE(session.start());
	auto inspection = session.beginInspection(1, {0}, 2s);
	ASSERT_TRUE(inspection);
	EXPECT_FALSE(session.requestCapture(inspection.value(), 0, std::chrono::steady_clock::now() - 1ms));
	ASSERT_TRUE(session.requestCapture(inspection.value(), 0, std::chrono::steady_clock::now() + 30ms));
	ASSERT_TRUE(results.waitFor(1));
	session.requestStop();
	session.wait();
	ASSERT_EQ(results.values.size(), 1U);
	EXPECT_EQ(results.values[0].decision, runtime::InspectionDecision::Fault);
	EXPECT_EQ(results.values[0].views[0].status.code(), core::StatusCode::DeadlineExceeded);
}

TEST(EventInspectionTest, ConcurrentTriggerEntryAndShutdownStillDeliverEveryInspectionOnce) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	ASSERT_TRUE(addRoute(session, 0));
	ASSERT_TRUE(session.start());
	std::vector<std::uint64_t> inspections;
	for (std::uint64_t workpiece = 1; workpiece <= 32; ++workpiece) {
		auto opened = session.beginInspection(workpiece, {0}, 2s);
		ASSERT_TRUE(opened);
		inspections.push_back(opened.value());
	}
	std::jthread producer([&] {
		for (const auto id : inspections) {
			auto requested = session.requestCapture(id, 0);
			if (!requested) {
				EXPECT_TRUE(requested.status().code() == core::StatusCode::InvalidState ||
					requested.status().code() == core::StatusCode::NotFound ||
					requested.status().code() == core::StatusCode::QueueFull);
			}
		}
	});
	session.requestStop();
	session.wait();
	producer.join();
	ASSERT_EQ(results.values.size(), inspections.size());
	std::vector<std::uint64_t> delivered;
	for (const auto& result : results.values) {
		delivered.push_back(result.inspectionId);
	}
	std::sort(delivered.begin(), delivered.end());
	EXPECT_EQ(delivered, inspections);
}

TEST(EventInspectionTest, ResultObserverCanRequestStopWithoutBlockingSessionDrain) {
	Results results;
	runtime::EventInspectionSession* active = nullptr;
	runtime::EventInspectionSession session([&](const auto& result) {
		active->requestStop();
		EXPECT_FALSE(addRoute(*active, 2));
		results.observe(result);
	});
	active = &session;
	ASSERT_TRUE(addRoute(session, 0));
	ASSERT_TRUE(session.start());
	auto inspection = session.beginInspection(1, {0}, 1s);
	ASSERT_TRUE(inspection);
	ASSERT_TRUE(session.requestCapture(inspection.value(), 0));
	ASSERT_TRUE(results.waitFor(1));
	session.wait();
	ASSERT_EQ(results.values.size(), 1U);
	EXPECT_EQ(results.values[0].decision, runtime::InspectionDecision::Ok);
}

TEST(EventInspectionTest, PipelineAdapterFailureIsFaultNotNgOrOk) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	auto source = camera::EventTriggerSource::create(std::make_unique<TestCamera>());
	ASSERT_TRUE(source);
	ASSERT_TRUE(session.addRoute<int>(0, std::move(source).value(),
		std::make_unique<ConstantPipeline<int>>(3),
		[](int) { return core::Result<runtime::ViewDecision>::failure(core::Status::error(
			core::StatusCode::BackendError, "mapping failed")); }));
	ASSERT_TRUE(session.start());
	auto inspection = session.beginInspection(1, {0}, 1s);
	ASSERT_TRUE(inspection);
	ASSERT_TRUE(session.requestCapture(inspection.value(), 0));
	ASSERT_TRUE(results.waitFor(1));
	session.requestStop();
	session.wait();
	EXPECT_EQ(results.values[0].decision, runtime::InspectionDecision::Fault);
	EXPECT_EQ(results.values[0].views[0].status.code(), core::StatusCode::BackendError);
}

TEST(EventInspectionTest, StartFailureStopsAlreadyStartedSources) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	ASSERT_TRUE(addRoute(session, 0));
	auto device = std::make_unique<TestCamera>();
	auto* raw = device.get();
	raw->failStart = true;
	auto source = camera::EventTriggerSource::create(std::move(device));
	ASSERT_TRUE(source);
	ASSERT_TRUE(session.addRoute<bool>(1, std::move(source).value(),
		std::make_unique<ConstantPipeline<bool>>(true),
		[](bool) { return core::Result<runtime::ViewDecision>::success(runtime::ViewDecision::Ok); }));
	EXPECT_FALSE(session.start());
	EXPECT_FALSE(session.beginInspection(1, {0}, 1s));
	session.wait();
	EXPECT_TRUE(results.values.empty());
}

TEST(EventInspectionTest, WaitDoesNotStopAndObserversPreserveBusinessCallbacksAndStatistics) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	ASSERT_TRUE(addRoute(session, 7, false));
	std::atomic_size_t frames = 0, outcomes = 0;
	ASSERT_TRUE(session.addObservers({
		.result = [&](auto kind, const auto& identity, auto task, const auto& result) {
			EXPECT_EQ(kind, executor::FrameOutcome::Completed);
			EXPECT_TRUE(task);
			EXPECT_TRUE(result);
			EXPECT_EQ(identity.sourceId, 0U);
			EXPECT_EQ(identity.metadata.capture->viewId, 7U);
			++outcomes;
		},
		.frame = [&](auto source, const auto&) { EXPECT_EQ(source, 0U); ++frames; }}));
	ASSERT_TRUE(session.addObservers({.frame = [](auto, const auto&) { throw std::runtime_error("observer"); }}));
	ASSERT_EQ(session.sourceCount(), 1U);
	EXPECT_EQ(session.viewId(0), 7U);
	EXPECT_NE(session.cameraDevice(0), nullptr);
	EXPECT_EQ(session.cameraDevice(1), nullptr);
	ASSERT_TRUE(session.start());
	EXPECT_FALSE(session.addObservers({}));
	std::atomic_bool returned = false;
	std::jthread waiter([&] { session.wait(); returned = true; });
	std::this_thread::sleep_for(30ms);
	EXPECT_FALSE(returned);
	EXPECT_FALSE(session.stopRequested());
	auto inspection = session.beginInspection(20, {7}, 1s);
	EXPECT_TRUE(inspection);
	if (inspection) { EXPECT_TRUE(session.requestCapture(inspection.value(), 7)); }
	EXPECT_TRUE(results.waitFor(1));
	session.requestStop();
	waiter.join();
	EXPECT_TRUE(returned);
	EXPECT_EQ(frames, 1U);
	EXPECT_EQ(outcomes, 1U);
	const auto summary = session.currentSummary();
	EXPECT_EQ(summary.frames.received, 1U);
	EXPECT_EQ(summary.frames.submitted, 1U);
	EXPECT_EQ(summary.frames.completed, 1U);
	EXPECT_EQ(summary.frames.failed, 0U);
	EXPECT_EQ(summary.inspectionsStarted, 1U);
	EXPECT_EQ(summary.inspectionsInFlight, 0U);
	EXPECT_EQ(summary.inspectionsCompleted, 1U);
	EXPECT_EQ(summary.ng, 1U);
	EXPECT_EQ(summary.fault, 0U);
}

TEST(EventInspectionTest, IdleUnsolicitedFramePublishesTerminalFaultAndWakesWait) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	auto device = std::make_unique<TestCamera>(false);
	auto* raw = device.get();
	auto source = camera::EventTriggerSource::create(std::move(device));
	ASSERT_TRUE(source);
	ASSERT_TRUE(session.addRoute<bool>(0, std::move(source).value(),
		std::make_unique<ConstantPipeline<bool>>(true),
		[](bool) { return core::Result<runtime::ViewDecision>::success(runtime::ViewDecision::Ok); }));
	std::atomic_size_t faults = 0;
	ASSERT_TRUE(session.addObservers({.sourceFault = [&](const auto& fault) {
		EXPECT_TRUE(fault.terminal);
		EXPECT_EQ(fault.status.code(), core::StatusCode::DataLoss);
		++faults;
	}}));
	ASSERT_TRUE(session.start());
	raw->emit();
	const auto deadline = std::chrono::steady_clock::now() + 1s;
	while (!session.stopRequested() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::yield();
	}
	EXPECT_TRUE(session.stopRequested());
	session.requestStop();
	session.wait();
	EXPECT_EQ(faults, 1U);
	EXPECT_EQ(session.currentSummary().frames.sourceFailures, 1U);
	EXPECT_TRUE(results.values.empty());
}

TEST(EventInspectionTest, SequenceGapsAreSeparateFromWorkpieceAndExecutionFailures) {
	Results results;
	runtime::EventInspectionSession session([&](const auto& result) { results.observe(result); });
	auto device = std::make_unique<TestCamera>(false);
	auto* raw = device.get();
	auto source = camera::EventTriggerSource::create(std::move(device));
	ASSERT_TRUE(source);
	ASSERT_TRUE(session.addRoute<bool>(7, std::move(source).value(),
		std::make_unique<ConstantPipeline<bool>>(true),
		[](bool) { return core::Result<runtime::ViewDecision>::success(runtime::ViewDecision::Ok); }));
	std::atomic_size_t missing = 0;
	ASSERT_TRUE(session.addObservers({.sourceFault = [&](const auto& fault) {
		EXPECT_FALSE(fault.terminal);
		EXPECT_EQ(fault.sourceId, 0U);
		EXPECT_EQ(fault.previousSequence, 100U);
		EXPECT_EQ(fault.currentSequence, 103U);
		missing += fault.missingFrames;
		EXPECT_EQ(session.currentSummary().frames.missingFrames, 2U);
	}}));
	ASSERT_TRUE(session.start());
	for (std::size_t index = 0; index < 2; ++index) {
		auto inspection = session.beginInspection(index + 1, {7}, 1s);
		ASSERT_TRUE(inspection);
		ASSERT_TRUE(session.requestCapture(inspection.value(), 7));
		ASSERT_TRUE(raw->awaitTriggers(index + 1));
		raw->emit(index == 0 ? 100 : 103);
		ASSERT_TRUE(results.waitFor(index + 1));
	}
	session.requestStop();
	session.wait();
	const auto summary = session.currentSummary();
	EXPECT_EQ(missing, 2U);
	EXPECT_EQ(summary.frames.missingFrames, 2U);
	EXPECT_EQ(summary.frames.missingPerSource, (std::vector<std::size_t>{2}));
	EXPECT_EQ(summary.frames.failed, 0U);
	EXPECT_EQ(summary.frames.sourceFailures, 0U);
	EXPECT_EQ(summary.ok, 2U);
}

} // namespace
