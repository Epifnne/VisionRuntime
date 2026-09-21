#include "executor/serialPipelineExecutor.hpp"
#include "memory/cpuAllocator.hpp"
#include "runtime/multiCameraSession.hpp"

#include <gtest/gtest.h>

#include <latch>
#include <memory>
#include <mutex>
#include <vector>

namespace {

using visionRuntime::camera::FrameCallback;
using visionRuntime::core::Result;
using visionRuntime::pipeline::PipelinePacket;
using visionRuntime::vision::Frame;

// A scripted source that delivers a fixed number of frames then completes.
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

	void requestStop() noexcept override {
		stopRequested_.store(true);
	}

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

// Fails on the second source to exercise start rollback.
class FailingSource final : public visionRuntime::camera::IFrameSource {
public:
	~FailingSource() override = default;

	Result<void> start(FrameCallback) override {
		return Result<void>::failure(visionRuntime::core::Status::error(
			visionRuntime::core::StatusCode::Internal, "source failed to start"));
	}

	void requestStop() noexcept override {}
	void wait() noexcept override {}
	[[nodiscard]] bool isRunning() const noexcept override { return false; }
	[[nodiscard]] visionRuntime::camera::FrameSourceInfo info() const override {
		return {};
	}
};

class SourceIdPipeline final
	: public visionRuntime::pipeline::IVisionPipeline<std::uint32_t> {
public:
	Result<std::uint32_t> run(PipelinePacket packet) override {
		std::lock_guard lock(mutex_);
		seenSourceIds_.push_back(packet.sourceId().value_or(999));
		return Result<std::uint32_t>::success(packet.sourceId().value_or(999));
	}

	[[nodiscard]] std::vector<std::uint32_t> seenSourceIds() const {
		std::lock_guard lock(mutex_);
		return seenSourceIds_;
	}

private:
	mutable std::mutex mutex_;
	std::vector<std::uint32_t> seenSourceIds_;
};

} // namespace

TEST(MultiCameraSessionTest, RunsAllSourcesThroughOneExecutor) {
	using namespace visionRuntime;

	std::vector<std::unique_ptr<camera::IFrameSource>> sources;
	sources.push_back(std::make_unique<FiniteSource>(3));
	sources.push_back(std::make_unique<FiniteSource>(2));

	auto pipeline = std::make_unique<SourceIdPipeline>();
	auto* rawPipeline = pipeline.get();
	executor::ExecutorOptions options{
		.queueCapacity = 16,
		.queueFullPolicy = executor::QueueFullPolicy::Block,
		.stageQueueCapacity = 4,
	};
	auto executor = std::make_unique<executor::SerialPipelineExecutor<std::uint32_t>>(
		std::move(pipeline), options);

	runtime::MultiCameraSession<std::uint32_t> session(
		std::move(sources), std::move(executor));
	ASSERT_TRUE(session.start()) ;
	auto summary = session.wait();

	EXPECT_EQ(summary.received, 5U);
	EXPECT_EQ(summary.submitted, 5U);
	EXPECT_EQ(summary.completed, 5U);
	EXPECT_EQ(summary.failed, 0U);
	EXPECT_EQ(summary.receivedPerSource.size(), 2U);
	EXPECT_EQ(summary.receivedPerSource[0], 3U);
	EXPECT_EQ(summary.receivedPerSource[1], 2U);

	auto seen = rawPipeline->seenSourceIds();
	EXPECT_EQ(std::count(seen.begin(), seen.end(), 0U), 3);
	EXPECT_EQ(std::count(seen.begin(), seen.end(), 1U), 2);
}

TEST(MultiCameraSessionTest, RollsBackStartedSourcesWhenALaterStartFails) {
	using namespace visionRuntime;

	std::vector<std::unique_ptr<camera::IFrameSource>> sources;
	sources.push_back(std::make_unique<FiniteSource>(100));
	sources.push_back(std::make_unique<FailingSource>());

	auto pipeline = std::make_unique<SourceIdPipeline>();
	executor::ExecutorOptions options{
		.queueCapacity = 16,
		.queueFullPolicy = executor::QueueFullPolicy::Block,
		.stageQueueCapacity = 4,
	};
	auto executor = std::make_unique<executor::SerialPipelineExecutor<std::uint32_t>>(
		std::move(pipeline), options);

	runtime::MultiCameraSession<std::uint32_t> session(
		std::move(sources), std::move(executor));
	auto started = session.start();
	EXPECT_FALSE(started);
	EXPECT_EQ(started.status().code(), core::StatusCode::Internal);
}
