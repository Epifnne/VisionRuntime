#include "executor/pipelineExecutor.hpp"

#include <gtest/gtest.h>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {

class BlockingPipeline final : public visonRuntime::pipeline::IVisionPipeline<int> {
public:
	visonRuntime::core::Result<int> run(
		visonRuntime::pipeline::PipelinePacket) override {
		std::unique_lock lock(mutex_);
		started_ = true;
		startedReady_.notify_all();
		releaseReady_.wait(lock, [this] { return released_; });
		return visonRuntime::core::Result<int>::success(++result_);
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

class SequencePipeline final : public visonRuntime::pipeline::IVisionPipeline<int> {
public:
	visonRuntime::core::Result<int> run(
		visonRuntime::pipeline::PipelinePacket) override {
		return visonRuntime::core::Result<int>::success(++result_);
	}

private:
	int result_ = 0;
};

class ThrowingPipeline final : public visonRuntime::pipeline::IVisionPipeline<int> {
public:
	visonRuntime::core::Result<int> run(
		visonRuntime::pipeline::PipelinePacket) override {
		throw std::runtime_error("failure");
	}
};

} // namespace

TEST(PipelineExecutorTest, RejectsSubmissionWhenEntryQueueIsFull) {
	auto pipeline = std::make_unique<BlockingPipeline>();
	auto* pipelinePointer = pipeline.get();
	visonRuntime::executor::ExecutorOptions options;
	options.queueCapacity = 1;
	visonRuntime::executor::PipelineExecutor<int> executor(
		std::move(pipeline), options);

	auto running = executor.submit(visonRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(running);
	pipelinePointer->waitUntilStarted();
	auto queued = executor.submit(visonRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(queued);
	auto rejected = executor.submit(visonRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(rejected);
	EXPECT_EQ(rejected.status().code(), visonRuntime::core::StatusCode::QueueFull);
	pipelinePointer->release();
	executor.stop();
	EXPECT_TRUE(running->future().get());
	EXPECT_TRUE(queued->future().get());
}

TEST(PipelineExecutorTest, DeliversResultsAndCallbacksInSubmissionOrder) {
	visonRuntime::executor::PipelineExecutor<int> executor(
		std::make_unique<SequencePipeline>(), 3);
	std::mutex callbackMutex;
	std::vector<visonRuntime::executor::TaskId> callbacks;
	auto callback = [&](visonRuntime::executor::TaskId id, const auto&) {
		std::lock_guard lock(callbackMutex);
		callbacks.push_back(id);
	};

	auto first = executor.submit(visonRuntime::pipeline::PipelinePacket({}), callback);
	auto second = executor.submit(visonRuntime::pipeline::PipelinePacket({}), callback);
	ASSERT_TRUE(first);
	ASSERT_TRUE(second);
	EXPECT_EQ(first->future().get().value(), 1);
	EXPECT_EQ(second->future().get().value(), 2);
	executor.stop();

	EXPECT_EQ(callbacks, (std::vector<visonRuntime::executor::TaskId>{
		first->id(), second->id()}));
}

TEST(PipelineExecutorTest, ConvertsPipelineAndCallbackExceptions) {
	visonRuntime::executor::PipelineExecutor<int> executor(
		std::make_unique<ThrowingPipeline>());
	auto task = executor.submit(
		visonRuntime::pipeline::PipelinePacket({}),
		[](auto, const auto&) { throw std::runtime_error("callback failure"); });
	ASSERT_TRUE(task);

	const auto& result = task->future().get();
	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visonRuntime::core::StatusCode::Internal);
	EXPECT_EQ(task->state(), visonRuntime::executor::TaskState::Failed);
}

TEST(PipelineExecutorTest, CancelsQueuedTaskWithoutExecutingIt) {
	auto pipeline = std::make_unique<BlockingPipeline>();
	auto* pipelinePointer = pipeline.get();
	visonRuntime::executor::PipelineExecutor<int> executor(std::move(pipeline), 1);
	auto running = executor.submit(visonRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(running);
	pipelinePointer->waitUntilStarted();
	auto queued = executor.submit(visonRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(queued);

	EXPECT_TRUE(queued->cancel());
	pipelinePointer->release();
	executor.stop();
	EXPECT_EQ(queued->future().get().status().code(),
		visonRuntime::core::StatusCode::Cancelled);
	EXPECT_EQ(queued->state(), visonRuntime::executor::TaskState::Cancelled);
}

TEST(PipelineExecutorTest, ImmediateStopCancelsAcceptedTasks) {
	auto pipeline = std::make_unique<BlockingPipeline>();
	auto* pipelinePointer = pipeline.get();
	visonRuntime::executor::PipelineExecutor<int> executor(std::move(pipeline), 1);
	std::vector<visonRuntime::executor::TaskId> callbacks;
	auto callback = [&callbacks](auto id, const auto&) { callbacks.push_back(id); };
	auto running = executor.submit(
		visonRuntime::pipeline::PipelinePacket({}), callback);
	ASSERT_TRUE(running);
	pipelinePointer->waitUntilStarted();
	auto queued = executor.submit(
		visonRuntime::pipeline::PipelinePacket({}), callback);
	ASSERT_TRUE(queued);

	pipelinePointer->release();
	executor.stop(visonRuntime::executor::StopMode::Immediate);
	EXPECT_EQ(running->future().get().status().code(),
		visonRuntime::core::StatusCode::Cancelled);
	EXPECT_EQ(queued->future().get().status().code(),
		visonRuntime::core::StatusCode::Cancelled);
	EXPECT_EQ(callbacks, (std::vector<visonRuntime::executor::TaskId>{
		running->id(), queued->id()}));
}