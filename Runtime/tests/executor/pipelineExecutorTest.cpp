#include "common/boundedBlockingQueue.hpp"
#include "core/completionDispatcher.hpp"
#include "executor/executorTask.hpp"
#include "executor/frameExecutor.hpp"
#include "executor/parallelPipelineExecutor.hpp"
#include "executor/serialPipelineExecutor.hpp"
#include "runtime/runtimeFactory.hpp"

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

class OverlapPipeline final
	: public visionRuntime::pipeline::IStagedVisionPipeline<int> {
public:
	visionRuntime::core::Result<visionRuntime::preprocess::PreparedInput> preprocess(
		visionRuntime::pipeline::PipelinePacket packet) override {
		{
			std::lock_guard lock(mutex_);
			++preprocessed_;
		}
		preprocessReady_.notify_all();
		return visionRuntime::core::Result<
			visionRuntime::preprocess::PreparedInput>::success({
				std::move(packet), {}});
	}

	visionRuntime::core::Result<visionRuntime::pipeline::InferenceOutput> infer(
		visionRuntime::preprocess::PreparedInput input) override {
		std::unique_lock lock(mutex_);
		if (!inferenceStarted_) {
			inferenceStarted_ = true;
			inferenceReady_.notify_all();
			inferenceReleaseReady_.wait(lock, [this] { return inferenceReleased_; });
		}
		return visionRuntime::core::Result<
			visionRuntime::pipeline::InferenceOutput>::success({
				std::move(input.packet()), {}, input.transformContext()});
	}

	visionRuntime::core::Result<int> postprocess(
		visionRuntime::pipeline::InferenceOutput) override {
		return visionRuntime::core::Result<int>::success(++result_);
	}

	visionRuntime::core::Result<int> run(
		visionRuntime::pipeline::PipelinePacket packet) override {
		auto prepared = preprocess(std::move(packet));
		if (!prepared) {
			return visionRuntime::core::Result<int>::failure(prepared.status());
		}
		auto output = infer(std::move(prepared).value());
		if (!output) {
			return visionRuntime::core::Result<int>::failure(output.status());
		}
		return postprocess(std::move(output).value());
	}

	void waitUntilInferenceStarted() {
		std::unique_lock lock(mutex_);
		inferenceReady_.wait(lock, [this] { return inferenceStarted_; });
	}

	void waitUntilPreprocessed(std::size_t count) {
		std::unique_lock lock(mutex_);
		preprocessReady_.wait(lock, [this, count] { return preprocessed_ >= count; });
	}

	void releaseInference() {
		{
			std::lock_guard lock(mutex_);
			inferenceReleased_ = true;
		}
		inferenceReleaseReady_.notify_all();
	}

private:
	std::mutex mutex_;
	std::condition_variable preprocessReady_;
	std::condition_variable inferenceReady_;
	std::condition_variable inferenceReleaseReady_;
	std::size_t preprocessed_ = 0;
	bool inferenceStarted_ = false;
	bool inferenceReleased_ = false;
	int result_ = 0;
};

class CountingSource final : public visionRuntime::camera::IFrameSource {
public:
	~CountingSource() override {
		requestStop();
		wait();
	}

	visionRuntime::core::Result<void> start(
		visionRuntime::camera::FrameCallback callback) override {
		running_ = true;
		stopSource_ = std::stop_source{};
		auto stopToken = stopSource_.get_token();
		worker_ = std::jthread(
			[this, callback = std::move(callback), stopToken](std::stop_token) mutable {
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

	void requestStop() noexcept override {
		stopSource_.request_stop();
	}

	void wait() noexcept override {
		if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
			worker_.join();
		}
		running_ = false;
	}

	[[nodiscard]] bool isRunning() const noexcept override {
		return running_;
	}

private:
	std::stop_source stopSource_;
	std::jthread worker_;
	std::atomic_bool running_ = false;
};

} // namespace

TEST(BoundedBlockingQueueTest, PreservesOrderAndDrainsAfterClose) {
	visionRuntime::common::BoundedBlockingQueue<int> queue(2);
	ASSERT_TRUE(queue.push(1));
	ASSERT_TRUE(queue.push(2));
	queue.close();
	ASSERT_EQ(queue.pop(), 1);
	ASSERT_EQ(queue.pop(), 2);
	EXPECT_FALSE(queue.pop());
}

TEST(BoundedBlockingQueueTest, RejectsPushForZeroCapacity) {
	visionRuntime::common::BoundedBlockingQueue<int> queue(0);
	EXPECT_FALSE(queue.push(1));
	queue.close();
	EXPECT_FALSE(queue.pop());
}

TEST(BoundedBlockingQueueTest, WakesBlockedProducerAfterPop) {
	visionRuntime::common::BoundedBlockingQueue<int> queue(1);
	ASSERT_TRUE(queue.push(1));
	auto pushed = std::async(std::launch::async, [&queue] {
		return queue.push(2);
	});
	EXPECT_EQ(pushed.wait_for(std::chrono::milliseconds(20)),
		std::future_status::timeout);
	EXPECT_EQ(queue.pop(), 1);
	EXPECT_TRUE(pushed.get());
	EXPECT_EQ(queue.pop(), 2);
}

TEST(BoundedBlockingQueueTest, CloseWakesBlockedConsumer) {
	visionRuntime::common::BoundedBlockingQueue<int> queue(1);
	auto popped = std::async(std::launch::async, [&queue] {
		return queue.pop();
	});
	EXPECT_EQ(popped.wait_for(std::chrono::milliseconds(20)),
		std::future_status::timeout);
	queue.close();
	EXPECT_FALSE(popped.get());
}

TEST(CompletionDispatcherTest, DeliversResultOnWorkerThread) {
	visionRuntime::core::CompletionDispatcher<int> dispatcher;
	std::promise<std::thread::id> delivered;
	const auto caller = std::this_thread::get_id();
	ASSERT_TRUE(dispatcher.dispatch(
		visionRuntime::core::Result<int>::success(42),
		[&delivered](auto result) {
			EXPECT_EQ(result.value(), 42);
			delivered.set_value(std::this_thread::get_id());
		}));
	EXPECT_NE(delivered.get_future().get(), caller);
	dispatcher.closeInput();
	dispatcher.wait();
}

TEST(ExecutorTaskTest, CompletesHandleAndCallback) {
	bool callbackCalled = false;
	visionRuntime::executor::ExecutorTask<int> task(
		1, visionRuntime::pipeline::PipelinePacket({}),
		[&callbackCalled](auto, const auto& result) {
			callbackCalled = result && result.value() == 42;
		});
	auto handle = task.handle();
	task.markRunning();
	task.complete(visionRuntime::core::Result<int>::success(42));

	EXPECT_EQ(handle.state(), visionRuntime::executor::TaskState::Completed);
	EXPECT_EQ(handle.future().get().value(), 42);
	EXPECT_TRUE(callbackCalled);
}

TEST(PipelineExecutorTest, RejectsSubmissionWhenEntryQueueIsFull) {
	auto pipeline = std::make_unique<BlockingPipeline>();
	auto* pipelinePointer = pipeline.get();
	visionRuntime::executor::ExecutorOptions options;
	options.queueCapacity = 1;
	visionRuntime::executor::SerialPipelineExecutor<int> executor(
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
	executor.requestStop();
	executor.wait();
	EXPECT_TRUE(running->future().get());
	EXPECT_TRUE(queued->future().get());
}

TEST(PipelineExecutorTest, DeliversResultsAndCallbacksInSubmissionOrder) {
	visionRuntime::executor::SerialPipelineExecutor<int> executor(
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
	executor.requestStop();
	executor.wait();

	EXPECT_EQ(callbacks, (std::vector<visionRuntime::executor::TaskId>{
		first->id(), second->id()}));
}

TEST(PipelineExecutorTest, ConvertsPipelineAndCallbackExceptions) {
	visionRuntime::executor::SerialPipelineExecutor<int> executor(
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
	visionRuntime::executor::SerialPipelineExecutor<int> executor(
		std::move(pipeline), 1);
	auto running = executor.submit(visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(running);
	pipelinePointer->waitUntilStarted();
	auto queued = executor.submit(visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(queued);

	EXPECT_TRUE(queued->cancel());
	pipelinePointer->release();
	executor.requestStop();
	executor.wait();
	EXPECT_EQ(queued->future().get().status().code(),
		visionRuntime::core::StatusCode::Cancelled);
	EXPECT_EQ(queued->state(), visionRuntime::executor::TaskState::Cancelled);
}

TEST(PipelineExecutorTest, ImmediateStopCancelsAcceptedTasks) {
	auto pipeline = std::make_unique<BlockingPipeline>();
	auto* pipelinePointer = pipeline.get();
	visionRuntime::executor::SerialPipelineExecutor<int> executor(
		std::move(pipeline), 1);
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
	executor.requestStop(visionRuntime::executor::StopMode::Immediate);
	executor.wait();
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
	visionRuntime::executor::SerialPipelineExecutor<int> executor(
		std::move(pipeline), options);

	ASSERT_TRUE(executor.submit(visionRuntime::pipeline::PipelinePacket({})));
	pipelinePointer->waitUntilStarted();
	ASSERT_TRUE(executor.submit(visionRuntime::pipeline::PipelinePacket({})));
	auto blocked = std::async(std::launch::async, [&executor] {
		return executor.submit(visionRuntime::pipeline::PipelinePacket({}));
	});
	EXPECT_EQ(blocked.wait_for(std::chrono::milliseconds(20)),
		std::future_status::timeout);
	executor.requestStop();
	auto blockedResult = blocked.get();
	EXPECT_FALSE(blockedResult);
	EXPECT_EQ(blockedResult.status().code(),
		visionRuntime::core::StatusCode::InvalidState);
	pipelinePointer->release();
	executor.wait();
}

TEST(ParallelPipelineExecutorTest, OverlapsStagesAndPreservesOrder) {
	auto pipeline = std::make_unique<OverlapPipeline>();
	auto* pipelinePointer = pipeline.get();
	visionRuntime::executor::ParallelPipelineExecutor<int> executor(
		std::move(pipeline));

	auto first = executor.submit(visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(first);
	pipelinePointer->waitUntilInferenceStarted();
	auto second = executor.submit(visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(second);
	pipelinePointer->waitUntilPreprocessed(2);
	pipelinePointer->releaseInference();

	EXPECT_EQ(first->future().get().value(), 1);
	EXPECT_EQ(second->future().get().value(), 2);
	executor.requestStop();
	executor.wait();
}

TEST(ParallelPipelineExecutorTest, RejectsZeroStageQueueCapacity) {
	visionRuntime::executor::ExecutorOptions options;
	options.stageQueueCapacity = 0;
	visionRuntime::executor::ParallelPipelineExecutor<int> executor(
		std::make_unique<OverlapPipeline>(), options);

	auto submitted = executor.submit(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(submitted);
	EXPECT_EQ(submitted.status().code(),
		visionRuntime::core::StatusCode::InvalidArgument);
}

TEST(ParallelPipelineExecutorTest, ImmediateStopCancelsRunningStage) {
	auto pipeline = std::make_unique<OverlapPipeline>();
	auto* pipelinePointer = pipeline.get();
	visionRuntime::executor::ParallelPipelineExecutor<int> executor(
		std::move(pipeline));
	auto submitted = executor.submit(
		visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(submitted);
	pipelinePointer->waitUntilInferenceStarted();

	executor.requestStop(visionRuntime::executor::StopMode::Immediate);
	for (;;) {
		auto probe = executor.submit(
			visionRuntime::pipeline::PipelinePacket({}));
		if (!probe && probe.status().code() ==
			visionRuntime::core::StatusCode::InvalidState) {
			break;
		}
		std::this_thread::yield();
	}
	pipelinePointer->releaseInference();
	executor.wait();

	EXPECT_EQ(submitted->future().get().status().code(),
		visionRuntime::core::StatusCode::Cancelled);
}

TEST(RuntimeFactoryTest, CreatesConfiguredExecutorStrategy) {
	visionRuntime::config::DeploymentConfig config;
	auto serial = visionRuntime::runtime::RuntimeFactory::createExecutor<int>(
		std::make_unique<SequencePipeline>(), config);
	ASSERT_TRUE(serial);
	EXPECT_NE(dynamic_cast<visionRuntime::executor::SerialPipelineExecutor<int>*>(
		serial->get()), nullptr);
	(*serial)->requestStop();
	(*serial)->wait();

	config.executor.performancePolicy =
		visionRuntime::config::PerformancePolicy::PipelineParallel;
	auto parallel = visionRuntime::runtime::RuntimeFactory::createExecutor<int>(
		std::make_unique<OverlapPipeline>(), config);
	ASSERT_TRUE(parallel);
	EXPECT_NE(dynamic_cast<visionRuntime::executor::ParallelPipelineExecutor<int>*>(
		parallel->get()), nullptr);
	(*parallel)->requestStop();
	(*parallel)->wait();
}

TEST(RuntimeFactoryTest, RejectsNonStagedPipelineForParallelPolicy) {
	visionRuntime::config::DeploymentConfig config;
	config.executor.performancePolicy =
		visionRuntime::config::PerformancePolicy::PipelineParallel;

	auto executor = visionRuntime::runtime::RuntimeFactory::createExecutor<int>(
		std::make_unique<SequencePipeline>(), config);

	EXPECT_FALSE(executor);
	EXPECT_EQ(executor.status().code(),
		visionRuntime::core::StatusCode::InvalidArgument);
}

TEST(RuntimeFactoryTest, CreatesRunnableSession) {
	using namespace visionRuntime;
	config::DeploymentConfig config;
	executor::FrameExecutionOptions<int> options;
	options.frameCount = 3;

	auto runtime = runtime::RuntimeFactory::createRuntime<int>(
		std::make_unique<CountingSource>(),
		std::make_unique<SequencePipeline>(), config, std::move(options));

	ASSERT_TRUE(runtime);
	ASSERT_TRUE((*runtime)->start());
	const auto summary = (*runtime)->wait();
	EXPECT_EQ(summary.received, 3U);
	EXPECT_EQ(summary.submitted, 2U);
	EXPECT_EQ(summary.completed, 2U);
}

TEST(ParallelPipelineExecutorTest, OverlapsStagesAndPreservesOrder) {
	auto pipeline = std::make_unique<OverlapPipeline>();
	auto* pipelinePointer = pipeline.get();
	visionRuntime::executor::ParallelPipelineExecutor<int> executor(
		std::move(pipeline));

	auto first = executor.submit(visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(first);
	pipelinePointer->waitUntilInferenceStarted();
	auto second = executor.submit(visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(second);
	pipelinePointer->waitUntilPreprocessed(2);
	pipelinePointer->releaseInference();

	EXPECT_EQ(first->future().get().value(), 1);
	EXPECT_EQ(second->future().get().value(), 2);
	executor.stop();
}

TEST(ParallelPipelineExecutorTest, RejectsZeroStageQueueCapacity) {
	visionRuntime::executor::ExecutorOptions options;
	options.stageQueueCapacity = 0;
	visionRuntime::executor::ParallelPipelineExecutor<int> executor(
		std::make_unique<OverlapPipeline>(), options);

	auto submitted = executor.submit(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(submitted);
	EXPECT_EQ(submitted.status().code(),
		visionRuntime::core::StatusCode::InvalidArgument);
}

TEST(ParallelPipelineExecutorTest, ImmediateStopCancelsRunningStage) {
	auto pipeline = std::make_unique<OverlapPipeline>();
	auto* pipelinePointer = pipeline.get();
	visionRuntime::executor::ParallelPipelineExecutor<int> executor(
		std::move(pipeline));
	auto submitted = executor.submit(
		visionRuntime::pipeline::PipelinePacket({}));
	ASSERT_TRUE(submitted);
	pipelinePointer->waitUntilInferenceStarted();

	auto stopping = std::async(std::launch::async, [&executor] {
		executor.stop(visionRuntime::executor::StopMode::Immediate);
	});
	for (;;) {
		auto probe = executor.submit(
			visionRuntime::pipeline::PipelinePacket({}));
		if (!probe && probe.status().code() ==
			visionRuntime::core::StatusCode::InvalidState) {
			break;
		}
		std::this_thread::yield();
	}
	pipelinePointer->releaseInference();
	stopping.get();

	EXPECT_EQ(submitted->future().get().status().code(),
		visionRuntime::core::StatusCode::Cancelled);
}

TEST(RuntimeFactoryTest, CreatesConfiguredExecutorStrategy) {
	visionRuntime::config::DeploymentConfig config;
	auto serial = visionRuntime::runtime::RuntimeFactory::createExecutor<int>(
		std::make_unique<SequencePipeline>(), config);
	ASSERT_TRUE(serial);
	EXPECT_NE(dynamic_cast<visionRuntime::executor::SerialPipelineExecutor<int>*>(
		serial->get()), nullptr);
	(*serial)->stop();

	config.executor.performancePolicy =
		visionRuntime::config::PerformancePolicy::PipelineParallel;
	auto parallel = visionRuntime::runtime::RuntimeFactory::createExecutor<int>(
		std::make_unique<OverlapPipeline>(), config);
	ASSERT_TRUE(parallel);
	EXPECT_NE(dynamic_cast<visionRuntime::executor::ParallelPipelineExecutor<int>*>(
		parallel->get()), nullptr);
	(*parallel)->stop();
}

TEST(RuntimeFactoryTest, RejectsNonStagedPipelineForParallelPolicy) {
	visionRuntime::config::DeploymentConfig config;
	config.executor.performancePolicy =
		visionRuntime::config::PerformancePolicy::PipelineParallel;

	auto executor = visionRuntime::runtime::RuntimeFactory::createExecutor<int>(
		std::make_unique<SequencePipeline>(), config);

	EXPECT_FALSE(executor);
	EXPECT_EQ(executor.status().code(),
		visionRuntime::core::StatusCode::InvalidArgument);
}

TEST(RuntimeFactoryTest, CreatesRunnableSession) {
	using namespace visionRuntime;
	config::DeploymentConfig config;
	executor::FrameExecutionOptions<int> options;
	options.frameCount = 3;

	auto runtime = runtime::RuntimeFactory::createRuntime<int>(
		std::make_unique<CountingSource>(),
		std::make_unique<SequencePipeline>(), config, std::move(options));

	ASSERT_TRUE(runtime);
	ASSERT_TRUE((*runtime)->start());
	const auto summary = (*runtime)->wait();
	EXPECT_EQ(summary.received, 3U);
	EXPECT_EQ(summary.submitted, 2U);
	EXPECT_EQ(summary.completed, 2U);
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
		std::make_unique<executor::SerialPipelineExecutor<int>>(
			std::make_unique<SequencePipeline>()),
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