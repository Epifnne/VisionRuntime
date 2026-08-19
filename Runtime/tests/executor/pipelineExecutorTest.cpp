#include "executor/frameExecutor.hpp"
#include "executor/pipelineExecutor.hpp"

#include <gtest/gtest.h>

#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

class BlockingPipeline final : public visionRuntime::pipeline::IVisionPipeline<int> {
public:
	visionRuntime::core::Result<int> run(
		visionRuntime::pipeline::PipelinePacket) override {
		std::unique_lock lock(mutex_);
		started_ = true;
		startedReady_.notify_all();
		releaseReady_.wait(lock, [this] { return released_; });
		return visionRuntime::core::Result<int>::success(++result_);
	}

	void waitUntilStarted() {
		std::unique_lock lock(mutex_);
		startedReady_.wait(lock, [this] { return started_; });
	}

	void release() {
		{
			std::lock_guard lock(mutex_);
			released_ = true;
		}
		releaseReady_.notify_all();
	}

private:
	std::mutex mutex_;
	std::condition_variable startedReady_;
	std::condition_variable releaseReady_;
	bool started_ = false;
	bool released_ = false;
	int result_ = 0;
};

class SequencePipeline final : public visionRuntime::pipeline::IVisionPipeline<int> {
public:
	visionRuntime::core::Result<int> run(
		visionRuntime::pipeline::PipelinePacket) override {
		return visionRuntime::core::Result<int>::success(++result_);
	}

private:
	int result_ = 0;
};

class ThrowingPipeline final : public visionRuntime::pipeline::IVisionPipeline<int> {
public:
	visionRuntime::core::Result<int> run(
		visionRuntime::pipeline::PipelinePacket) override {
		throw std::runtime_error("failure");
	}
};

class CountingSource final : public visionRuntime::camera::IFrameSource {
public:
	~CountingSource() override {
		static_cast<void>(stop());
	}

	visionRuntime::core::Result<void> start(
		visionRuntime::camera::FrameCallback callback) override {
		running_ = true;
		worker_ = std::jthread(
			[this, callback = std::move(callback)](std::stop_token stopToken) mutable {
				std::size_t index = 0;
				while (!stopToken.stop_requested()) {
					if (index++ == 0) {
						callback(visionRuntime::core::Result<visionRuntime::vision::Frame>::failure(
							visionRuntime::core::Status::error(
								visionRuntime::core::StatusCode::DataLoss, "test source failure")));
						continue;
					}
					auto buffer = visionRuntime::core::TensorBuffer::allocate(1);
					auto frame = visionRuntime::vision::Frame::create(
						std::move(buffer).value(), 1, 1,
						visionRuntime::vision::PixelFormat::Gray8);
					callback(std::move(frame));
				}
				running_ = false;
			});
		return visionRuntime::core::Result<void>::success();
	}

	visionRuntime::core::Result<void> stop() override {
		worker_.request_stop();
		if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
			worker_.join();
		}
		running_ = false;
		return visionRuntime::core::Result<void>::success();
	}

	[[nodiscard]] bool isRunning() const noexcept override {
		return running_;
	}

private:
	std::jthread worker_;
	std::atomic_bool running_ = false;
};

} // namespace

TEST(PipelineExecutorTest, RejectsSubmissionWhenEntryQueueIsFull) {
	auto pipeline = std::make_unique<BlockingPipeline>();
	auto* pipelinePointer = pipeline.get();
	visionRuntime::executor::ExecutorOptions options;
	options.queueCapacity = 1;
	visionRuntime::executor::PipelineExecutor<int> executor(
		std::move(pipeline), options);

	auto running = executor.submit(visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(running);
	pipelinePointer->waitUntilStarted();
	auto queued = executor.submit(visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(queued);
	auto rejected = executor.submit(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(rejected);
	EXPECT_EQ(rejected.status().code(), visionRuntime::core::StatusCode::QueueFull);
	pipelinePointer->release();
	executor.stop();
	EXPECT_TRUE(running->future().get());
	EXPECT_TRUE(queued->future().get());
}

TEST(PipelineExecutorTest, DeliversResultsAndCallbacksInSubmissionOrder) {
	visionRuntime::executor::PipelineExecutor<int> executor(
		std::make_unique<SequencePipeline>(), 3);
	std::mutex callbackMutex;
	std::vector<visionRuntime::executor::TaskId> callbacks;
	auto callback = [&](visionRuntime::executor::TaskId id, const auto&) {
		std::lock_guard lock(callbackMutex);
		callbacks.push_back(id);
	};

	auto first = executor.submit(visionRuntime::pipeline::PipelinePacket({}), callback);
	auto second = executor.submit(visionRuntime::pipeline::PipelinePacket({}), callback);
	ASSERT_TRUE(first);
	ASSERT_TRUE(second);
	EXPECT_EQ(first->future().get().value(), 1);
	EXPECT_EQ(second->future().get().value(), 2);
	executor.stop();

	EXPECT_EQ(callbacks, (std::vector<visionRuntime::executor::TaskId>{
		first->id(), second->id()}));
}

TEST(PipelineExecutorTest, ConvertsPipelineAndCallbackExceptions) {
	visionRuntime::executor::PipelineExecutor<int> executor(
		std::make_unique<ThrowingPipeline>());
	auto task = executor.submit(
		visionRuntime::pipeline::PipelinePacket({}),
		[](auto, const auto&) { throw std::runtime_error("callback failure"); });
	ASSERT_TRUE(task);

	const auto& result = task->future().get();
	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visionRuntime::core::StatusCode::Internal);
	EXPECT_EQ(task->state(), visionRuntime::executor::TaskState::Failed);
}

TEST(PipelineExecutorTest, CancelsQueuedTaskWithoutExecutingIt) {
	auto pipeline = std::make_unique<BlockingPipeline>();
	auto* pipelinePointer = pipeline.get();
	visionRuntime::executor::PipelineExecutor<int> executor(std::move(pipeline), 1);
	auto running = executor.submit(visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(running);
	pipelinePointer->waitUntilStarted();
	auto queued = executor.submit(visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(queued);

	EXPECT_TRUE(queued->cancel());
	pipelinePointer->release();
	executor.stop();
	EXPECT_EQ(queued->future().get().status().code(),
		visionRuntime::core::StatusCode::Cancelled);
	EXPECT_EQ(queued->state(), visionRuntime::executor::TaskState::Cancelled);
}

TEST(PipelineExecutorTest, ImmediateStopCancelsAcceptedTasks) {
	auto pipeline = std::make_unique<BlockingPipeline>();
	auto* pipelinePointer = pipeline.get();
	visionRuntime::executor::PipelineExecutor<int> executor(std::move(pipeline), 1);
	std::vector<visionRuntime::executor::TaskId> callbacks;
	auto callback = [&callbacks](auto id, const auto&) { callbacks.push_back(id); };
	auto running = executor.submit(
		visionRuntime::pipeline::PipelinePacket({}), callback);
	ASSERT_TRUE(running);
	pipelinePointer->waitUntilStarted();
	auto queued = executor.submit(
		visionRuntime::pipeline::PipelinePacket({}), callback);
	ASSERT_TRUE(queued);

	pipelinePointer->release();
	executor.stop(visionRuntime::executor::StopMode::Immediate);
	EXPECT_EQ(running->future().get().status().code(),
		visionRuntime::core::StatusCode::Cancelled);
	EXPECT_EQ(queued->future().get().status().code(),
		visionRuntime::core::StatusCode::Cancelled);
	EXPECT_EQ(callbacks, (std::vector<visionRuntime::executor::TaskId>{
		running->id(), queued->id()}));
}

TEST(PipelineExecutorTest, BlocksSubmissionUntilQueueHasSpace) {
	auto pipeline = std::make_unique<BlockingPipeline>();
	auto* pipelinePointer = pipeline.get();
	visionRuntime::executor::ExecutorOptions options;
	options.queueCapacity = 1;
	options.queueFullPolicy = visionRuntime::executor::QueueFullPolicy::Block;
	visionRuntime::executor::PipelineExecutor<int> executor(
		std::move(pipeline), options);

	ASSERT_TRUE(executor.submit(visionRuntime::pipeline::PipelinePacket({})));
	pipelinePointer->waitUntilStarted();
	ASSERT_TRUE(executor.submit(visionRuntime::pipeline::PipelinePacket({})));
	auto blocked = std::async(std::launch::async, [&executor] {
		return executor.submit(visionRuntime::pipeline::PipelinePacket({}));
	});
	EXPECT_EQ(blocked.wait_for(std::chrono::milliseconds(20)),
		std::future_status::timeout);
	pipelinePointer->release();
	EXPECT_TRUE(blocked.get());
	executor.stop();
}

TEST(FrameExecutorTest, AppliesFrameLimitAndSkipsSourceFailures) {
	using namespace visionRuntime;
	executor::FrameExecutionOptions<int> options;
	options.frameCount = 3;
	options.sourceFailurePolicy = executor::SourceFailurePolicy::Skip;
	std::vector<int> results;
	options.completionCallback = [&results](auto, const auto& result) {
		if (result) {
			results.push_back(result.value());
		}
	};

	executor::FrameExecutor<int> executor(
		std::make_unique<CountingSource>(),
		std::make_unique<SequencePipeline>(),
		std::move(options));
	ASSERT_TRUE(executor.start());
	const auto summary = executor.wait();

	EXPECT_EQ(summary.received, 3U);
	EXPECT_EQ(summary.sourceFailures, 1U);
	EXPECT_EQ(summary.submitted, 2U);
	EXPECT_EQ(summary.completed, 2U);
	EXPECT_EQ(summary.failed, 0U);
	EXPECT_EQ(results, (std::vector<int>{1, 2}));
}