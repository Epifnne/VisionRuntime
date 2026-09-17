#include "executor/serialPipelineExecutor.hpp"
#include "memory/cpuAllocator.hpp"
#include "pipeline/iVisionPipeline.hpp"
#include "pipeline/pipelinePacket.hpp"
#include "runtime/multiCameraSession.hpp"
#include "session/sessionController.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <latch>
#include <memory>
#include <thread>
#include <vector>

namespace {

using visionRuntime::camera::FrameCallback;
using visionRuntime::core::Result;
using visionRuntime::core::Status;
using visionRuntime::core::StatusCode;
using visionRuntime::pipeline::PipelinePacket;
using visionRuntime::vision::Frame;
using visionService::session::SessionController;
using visionService::session::SessionLifecycle;
using visionService::session::SessionResult;

class FiniteSource final : public visionRuntime::camera::IFrameSource {
public:
	explicit FiniteSource(std::size_t frameCount) : frameCount_(frameCount) {}

	~FiniteSource() override {
		requestStop();
		wait();
	}

	Result<void> start(FrameCallback callback) override {
		running_ = true;
		worker_ = std::thread(
			[this, callback = std::move(callback)]() mutable {
				for (std::size_t index = 0;
					index < frameCount_ && !stopRequested_.load(); ++index) {
					auto buffer = visionRuntime::memory::CpuAllocator{}.allocate(1);
					auto frame = Frame::create(std::move(buffer).value(), 1, 1,
						visionRuntime::vision::PixelFormat::Gray8);
					callback(std::move(frame));
				}
				running_ = false;
			});
		return Result<void>::success();
	}

	void requestStop() noexcept override { stopRequested_.store(true); }

	void wait() noexcept override {
		if (worker_.joinable() &&
			worker_.get_id() != std::this_thread::get_id()) {
			worker_.join();
		}
		running_ = false;
	}

	[[nodiscard]] bool isRunning() const noexcept override {
		return running_;
	}

	[[nodiscard]] visionRuntime::camera::FrameSourceInfo info() const override {
		return {
			.outputSpec = {{visionRuntime::vision::PixelFormat::Gray8}, 1, 1,
				visionRuntime::core::Device::cpu()},
			.expectedFrameCount = frameCount_,
			.isFinite = true,
		};
	}

private:
	std::size_t frameCount_;
	std::thread worker_;
	std::atomic_bool stopRequested_{false};
	std::atomic_bool running_{false};
};

class IdentityPipeline final
	: public visionRuntime::pipeline::IVisionPipeline<SessionResult> {
public:
	Result<SessionResult> run(PipelinePacket packet) override {
		static_cast<void>(packet);
		return Result<SessionResult>::success(SessionResult{});
	}
};

[[nodiscard]] std::unique_ptr<
	visionRuntime::runtime::MultiCameraSession<SessionResult>>
makeSession(std::size_t sourceCount, std::size_t framesPerSource) {
	std::vector<std::unique_ptr<visionRuntime::camera::IFrameSource>> sources;
	for (std::size_t index = 0; index < sourceCount; ++index) {
		sources.push_back(std::make_unique<FiniteSource>(framesPerSource));
	}
	auto pipeline = std::make_unique<IdentityPipeline>();
	visionRuntime::executor::ExecutorOptions options{
		.queueCapacity = 64,
		.queueFullPolicy = visionRuntime::executor::QueueFullPolicy::Block,
		.stageQueueCapacity = 4,
	};
	auto executor = std::make_unique<
		visionRuntime::executor::SerialPipelineExecutor<SessionResult>>(
		std::move(pipeline), options);
	return std::make_unique<visionRuntime::runtime::MultiCameraSession<
		SessionResult>>(std::move(sources), std::move(executor));
}

[[nodiscard]] bool waitForState(
	SessionController& controller,
	SessionLifecycle expected,
	std::chrono::milliseconds timeout) {
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (std::chrono::steady_clock::now() < deadline) {
		if (controller.state() == expected) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	return controller.state() == expected;
}

} // namespace

TEST(SessionControllerTest, StartsAsConfiguredThenIdle) {
	SessionController controller;
	EXPECT_EQ(controller.state(), SessionLifecycle::Idle);

	auto unconfigured = controller.start();
	ASSERT_FALSE(unconfigured);
	EXPECT_EQ(unconfigured.status().code(), StatusCode::InvalidState);

	ASSERT_TRUE(controller.configure(makeSession(1, 5), 1U));
	EXPECT_EQ(controller.state(), SessionLifecycle::Idle);
	EXPECT_EQ(controller.sourceCount(), 1U);
}

TEST(SessionControllerTest, RunsToCompletionAndCollectsCounters) {
	SessionController controller;
	ASSERT_TRUE(controller.configure(makeSession(2, 3), 2U));

	ASSERT_TRUE(controller.start()) << controller.lastError();
	EXPECT_EQ(controller.state(), SessionLifecycle::Running);

	ASSERT_TRUE(waitForState(
		controller, SessionLifecycle::Idle, std::chrono::seconds(10)));

	const auto counters = controller.counters();
	EXPECT_EQ(counters.received, 6U);
	EXPECT_EQ(counters.submitted, 6U);
	EXPECT_EQ(counters.completed, 6U);
	EXPECT_EQ(counters.failed, 0U);
	ASSERT_EQ(counters.receivedPerSource.size(), 2U);
	EXPECT_EQ(counters.receivedPerSource[0], 3U);
	EXPECT_EQ(counters.receivedPerSource[1], 3U);
	EXPECT_TRUE(controller.lastError().empty());
}

TEST(SessionControllerTest, RejectsDoubleStart) {
	SessionController controller;
	// A long-running source keeps the session in Running until stopped.
	ASSERT_TRUE(controller.configure(makeSession(1, 100000), 1U));
	ASSERT_TRUE(controller.start());

	auto second = controller.start();
	ASSERT_FALSE(second);
	EXPECT_EQ(second.status().code(), StatusCode::InvalidState);

	controller.requestStop(visionRuntime::executor::StopMode::Immediate);
	ASSERT_TRUE(waitForState(
		controller, SessionLifecycle::Idle, std::chrono::seconds(10)));
}

TEST(SessionControllerTest, StopTransitionsThroughStoppingToIdle) {
	SessionController controller;
	ASSERT_TRUE(controller.configure(makeSession(1, 100000), 1U));
	ASSERT_TRUE(controller.start());

	controller.requestStop();
	EXPECT_TRUE(controller.state() == SessionLifecycle::Stopping ||
		controller.state() == SessionLifecycle::Idle);
	ASSERT_TRUE(waitForState(
		controller, SessionLifecycle::Idle, std::chrono::seconds(10)));

	const auto counters = controller.counters();
	EXPECT_LE(counters.received, 100000U);
	EXPECT_EQ(counters.received, counters.submitted);
}
