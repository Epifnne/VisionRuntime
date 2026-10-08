#include "executor/batchPipelineExecutor.hpp"
#include "executor/parallelPipelineExecutor.hpp"
#include "executor/serialPipelineExecutor.hpp"
#include "memory/cpuAllocator.hpp"
#include "runtime/multiCameraSession.hpp"
#include "runtime/runtimeFactory.hpp"
#include "benchmark/latencyWindow.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace visionRuntime;
using core::Result;
using core::Status;
using core::StatusCode;
using pipeline::PipelinePacket;
using preprocess::PreparedInput;
using pipeline::InferenceOutput;

PipelinePacket packet(std::uint64_t sequence = 42,
	std::chrono::steady_clock::time_point captured = {}) {
	auto buffer = memory::CpuAllocator{}.allocate(1).value();
	auto frame = vision::Frame::create(std::move(buffer), 1, 1,
		vision::PixelFormat::Gray8, 1,
		{.sequenceNumber = sequence, .capturedAt = captured}).value();
	return PipelinePacket(std::move(frame), {}, 3);
}

class ContractPipeline final : public pipeline::IStagedVisionPipeline<int> {
public:
	explicit ContractPipeline(bool fail = false, bool shortBatch = false,
		bool failEvenFrames = false)
		: fail_(fail), shortBatch_(shortBatch), failEvenFrames_(failEvenFrames) {}

	Result<PreparedInput> preprocess(PipelinePacket input) override {
		if (failEvenFrames_ && input.identity().metadata.sequenceNumber % 2 == 0) {
			return Result<PreparedInput>::failure(Status::error(StatusCode::DataLoss,
				"scripted preprocessing failure"));
		}
		return Result<PreparedInput>::success({std::move(input), {}});
	}

	Result<InferenceOutput> infer(PreparedInput input) override {
		return Result<InferenceOutput>::success({std::move(input.packet()), {}, {}});
	}

	Result<std::vector<InferenceOutput>> inferBatch(
		std::vector<PreparedInput> inputs) override {
		std::vector<InferenceOutput> outputs;
		if (!shortBatch_) {
			for (auto& input : inputs) {
				outputs.emplace_back(std::move(input.packet()), preprocess::TensorMap{}, vision::TransformContext{});
			}
		}
		return Result<std::vector<InferenceOutput>>::success(std::move(outputs));
	}

	Result<int> postprocess(InferenceOutput) override {
		return fail_ ? Result<int>::failure(Status::error(StatusCode::DataLoss,
			"invalid output \"score\"")) : Result<int>::success(7);
	}

	Result<int> run(PipelinePacket input) override {
		auto prepared = preprocess(std::move(input)).value();
		return postprocess(infer(std::move(prepared)).value());
	}

private:
	bool fail_;
	bool shortBatch_;
	bool failEvenFrames_;
};

class BlockingPipeline final : public pipeline::IVisionPipeline<int> {
public:
	Result<int> run(PipelinePacket) override {
		std::unique_lock lock(mutex_);
		started_ = true;
		ready_.notify_all();
		ready_.wait(lock, [this] { return released_; });
		return Result<int>::success(1);
	}

	bool waitStarted() {
		std::unique_lock lock(mutex_);
		return ready_.wait_for(lock, std::chrono::seconds(5), [this] { return started_; });
	}

	void release() {
		std::lock_guard lock(mutex_);
		released_ = true;
		ready_.notify_all();
	}

private:
	std::mutex mutex_;
	std::condition_variable ready_;
	bool started_ = false;
	bool released_ = false;
};

class UntilStoppedSource final : public camera::IFrameSource {
public:
	UntilStoppedSource() : stoppedFuture_(stoppedPromise_.get_future()) {}
	~UntilStoppedSource() override { requestStop(); wait(); }

	Result<void> start(camera::FrameCallback callback) override {
		running_ = true;
		worker_ = std::thread([this, callback = std::move(callback)] {
			auto input = packet(1);
			auto frame = vision::Frame::create(input.cameraFrame()->buffer(),
				1, 1, vision::PixelFormat::Gray8, 1,
				input.cameraFrame()->metadata()).value();
			callback(Result<vision::Frame>::success(std::move(frame)));
			std::unique_lock lock(mutex_);
			ready_.wait(lock, [this] { return stopped_; });
			running_ = false;
		});
		return Result<void>::success();
	}
	void requestStop() noexcept override {
		std::lock_guard lock(mutex_);
		if (!stopped_) {
			stopped_ = true;
			stoppedPromise_.set_value();
		}
		ready_.notify_all();
	}
	void wait() noexcept override { if (worker_.joinable()) { worker_.join(); } }
	bool isRunning() const noexcept override { return running_; }
	camera::FrameSourceInfo info() const override { return {}; }
	std::future_status waitStopped(std::chrono::milliseconds timeout) {
		return stoppedFuture_.wait_for(timeout);
	}
private:
	std::mutex mutex_;
	std::condition_variable ready_;
	bool stopped_ = false;
	std::atomic_bool running_{false};
	std::thread worker_;
	std::promise<void> stoppedPromise_;
	std::future<void> stoppedFuture_;
};

class ScriptedSource final : public camera::IFrameSource {
public:
	ScriptedSource(std::vector<std::uint64_t> sequences, bool terminal = false,
		std::function<void()> afterFirst = {})
		: sequences_(std::move(sequences)), terminal_(terminal),
		  afterFirst_(std::move(afterFirst)) {}

	~ScriptedSource() override { requestStop(); wait(); }

	Result<void> start(camera::FrameCallback callback) override {
		running_ = true;
		worker_ = std::thread([this, callback = std::move(callback)] {
			if (terminal_) {
				callback(Result<vision::Frame>::failure(
					Status::error(StatusCode::Unavailable, "camera disconnected")));
			} else {
				for (const auto sequence : sequences_) {
					if (stopped_) {
						break;
					}
					auto input = packet(sequence);
					auto frame = vision::Frame::create(input.cameraFrame()->buffer(),
						1, 1, vision::PixelFormat::Gray8, 1,
						input.cameraFrame()->metadata()).value();
					callback(Result<vision::Frame>::success(std::move(frame)));
					if (sequence == sequences_.front() && afterFirst_) {
						afterFirst_();
					}
				}
			}
			running_ = false;
		});
		return Result<void>::success();
	}

	void requestStop() noexcept override { stopped_ = true; }
	void wait() noexcept override { if (worker_.joinable()) { worker_.join(); } }
	bool isRunning() const noexcept override { return running_; }
	camera::FrameSourceInfo info() const override { return {.isFinite = true}; }
	bool failureIsTerminal(const Status&) const noexcept override { return terminal_; }

private:
	std::vector<std::uint64_t> sequences_;
	bool terminal_;
	std::atomic_bool stopped_{false};
	std::atomic_bool running_{false};
	std::thread worker_;
	std::function<void()> afterFirst_;
};

} // namespace

TEST(ReleaseContractTest, IdentitySurvivesImageRelease) {
	auto input = packet();
	const auto identity = input.identity();
	input.completeImagePreparation();
	input.finishPostprocess();
	EXPECT_FALSE(input.hasCameraFrame());
	EXPECT_EQ(input.identity().executionId, identity.executionId);
	EXPECT_EQ(input.identity().metadata.sequenceNumber, 42U);
	EXPECT_EQ(input.identity().sourceId, 3U);
}

TEST(ReleaseContractTest, LatencySamplesRemainBoundedAndRepresentTheLatestWindow) {
	benchmark::LatencyWindow window;
	for (std::size_t index = 0; index < 2 * benchmark::LatencyWindow::capacity; ++index) {
		window.observe(static_cast<double>(index));
	}
	const auto snapshot = window.snapshot();
	EXPECT_EQ(snapshot.samples, benchmark::LatencyWindow::capacity);
	EXPECT_EQ(snapshot.p50Milliseconds, 6143.0);
	EXPECT_GE(snapshot.p95Milliseconds, snapshot.p50Milliseconds);
	EXPECT_GE(snapshot.p99Milliseconds, snapshot.p95Milliseconds);
}

TEST(ReleaseContractTest, EveryExecutorRejectsAlreadyExpiredFrames) {
	for (const auto mode : {0, 1, 2}) {
		config::DeploymentConfig deployment;
		deployment.executor.taskTimeout = std::chrono::milliseconds(10);
		deployment.executor.performancePolicy = mode == 0
			? config::PerformancePolicy::Serial : config::PerformancePolicy::PipelineParallel;
		auto created = mode == 2
			? runtime::RuntimeFactory::createBatchExecutor<int>(
				std::make_unique<ContractPipeline>(), deployment, {})
			: runtime::RuntimeFactory::createExecutor<int>(
				std::make_unique<ContractPipeline>(), deployment);
		ASSERT_TRUE(created);
		auto submitted = created.value()->submit(packet(42,
			std::chrono::steady_clock::now() - std::chrono::seconds(1)));
		ASSERT_FALSE(submitted);
		EXPECT_EQ(submitted.status().code(), StatusCode::DeadlineExceeded);
	}
}

TEST(ReleaseContractTest, LateCompletionIsNeverDeliveredAsSuccess) {
	bool called = false;
	executor::ExecutorTask<int> task(1, packet(),
		[&called](auto, const auto& result) {
			called = true;
			EXPECT_FALSE(result);
			EXPECT_EQ(result.status().code(), StatusCode::DeadlineExceeded);
		}, std::chrono::steady_clock::now() - std::chrono::seconds(1));
	auto handle = task.handle();
	task.complete(Result<int>::success(7));
	EXPECT_TRUE(called);
	EXPECT_EQ(handle.state(), executor::TaskState::Failed);
	EXPECT_EQ(handle.future().get().status().code(), StatusCode::DeadlineExceeded);
}

TEST(ReleaseContractTest, BlockSubmissionHonorsDeadlineWithoutDiscardingAcceptedTasks) {
	auto pipeline = std::make_unique<BlockingPipeline>();
	auto* blocking = pipeline.get();
	executor::ExecutorOptions options;
	options.queueCapacity = 1;
	options.queueFullPolicy = executor::QueueFullPolicy::Block;
	options.taskTimeout = std::chrono::seconds(1);
	executor::SerialPipelineExecutor<int> executor(std::move(pipeline), options);
	auto first = executor.submit(packet());
	const bool started = blocking->waitStarted();
	auto second = executor.submit(packet());
	auto third = executor.submit(packet());
	blocking->release();
	ASSERT_TRUE(first);
	ASSERT_TRUE(started);
	ASSERT_TRUE(second);
	ASSERT_FALSE(third);
	EXPECT_EQ(third.status().code(), StatusCode::DeadlineExceeded);
	executor.requestStop();
	executor.wait();
	EXPECT_EQ(first->future().get().status().code(), StatusCode::DeadlineExceeded);
	EXPECT_EQ(second->future().get().status().code(), StatusCode::DeadlineExceeded);
}

TEST(ReleaseContractTest, RejectsInvalidTimeoutInFactoryAndDirectExecutor) {
	config::DeploymentConfig deployment;
	deployment.executor.taskTimeout = std::chrono::milliseconds(-1);
	EXPECT_FALSE(runtime::RuntimeFactory::createExecutor<int>(
		std::make_unique<ContractPipeline>(), deployment));
	executor::ExecutorOptions options;
	options.taskTimeout = std::chrono::milliseconds(0);
	executor::SerialPipelineExecutor<int> executor(std::make_unique<ContractPipeline>(), options);
	auto result = executor.submit(packet());
	ASSERT_FALSE(result);
	EXPECT_EQ(result.status().code(), StatusCode::InvalidArgument);
}

TEST(ReleaseContractTest, RejectsBatchOutputCountMismatch) {
	config::DeploymentConfig deployment;
	auto created = runtime::RuntimeFactory::createBatchExecutor<int>(
		std::make_unique<ContractPipeline>(false, true), deployment, {});
	ASSERT_TRUE(created);
	auto submitted = created.value()->submit(packet());
	ASSERT_TRUE(submitted);
	created.value()->requestStop();
	created.value()->wait();
	ASSERT_FALSE(submitted->future().get());
	EXPECT_EQ(submitted->future().get().status().code(), StatusCode::ShapeMismatch);
}

TEST(ReleaseContractTest, ReportsAcquisitionGapsSeparatelyFromQueueDropsAndCorrelatesResults) {
	std::vector<std::unique_ptr<camera::IFrameSource>> sources;
	sources.push_back(std::make_unique<ScriptedSource>(std::vector<std::uint64_t>{10, 13, 14}));
	executor::ExecutorOptions executorOptions;
	executorOptions.queueFullPolicy = executor::QueueFullPolicy::Block;
	runtime::MultiCameraExecutionOptions<int> options;
	std::vector<std::uint64_t> sequences;
	std::mutex mutex;
	options.resultObserver = [&mutex, &sequences](auto outcome, const auto& identity,
		auto taskId, const auto& result) {
		std::lock_guard lock(mutex);
		EXPECT_EQ(outcome, executor::FrameOutcome::Completed);
		EXPECT_EQ(identity.sourceId, 0U);
		EXPECT_TRUE(taskId);
		EXPECT_TRUE(result);
		sequences.push_back(identity.metadata.sequenceNumber);
	};
	std::uint64_t reportedMissing = 0;
	options.sourceFaultObserver = [&reportedMissing](const auto& fault) {
		reportedMissing += fault.missingFrames;
		EXPECT_FALSE(fault.terminal);
		EXPECT_EQ(fault.previousSequence, 10U);
		EXPECT_EQ(fault.currentSequence, 13U);
	};
	runtime::MultiCameraSession<int> session(std::move(sources),
		std::make_unique<executor::SerialPipelineExecutor<int>>(
			std::make_unique<ContractPipeline>(), executorOptions), options);
	ASSERT_TRUE(session.start());
	const auto summary = session.wait();
	EXPECT_EQ(summary.missingFrames, 2U);
	EXPECT_EQ(summary.missingPerSource, std::vector<std::uint64_t>{2});
	EXPECT_EQ(summary.dropped, 0U);
	EXPECT_EQ(reportedMissing, 2U);
	EXPECT_EQ(sequences, (std::vector<std::uint64_t>{10, 13, 14}));
}

TEST(ReleaseContractTest, TerminalSourceFailureStopsSessionEvenWithContinuePolicy) {
	std::vector<std::unique_ptr<camera::IFrameSource>> sources;
	sources.push_back(std::make_unique<ScriptedSource>(std::vector<std::uint64_t>{}, true));
	runtime::MultiCameraExecutionOptions<int> options;
	bool terminalReported = false;
	options.sourceFaultObserver = [&terminalReported](const auto& fault) {
		terminalReported = fault.terminal;
		EXPECT_EQ(fault.sourceId, 0U);
	};
	runtime::MultiCameraSession<int> session(std::move(sources),
		std::make_unique<executor::SerialPipelineExecutor<int>>(
			std::make_unique<ContractPipeline>()), options);
	ASSERT_TRUE(session.start());
	const auto summary = session.wait();
	EXPECT_TRUE(terminalReported);
	EXPECT_TRUE(session.stopRequested());
	EXPECT_EQ(summary.sourceFailures, 1U);
	EXPECT_FALSE(summary.lastError.empty());
}

TEST(ReleaseContractTest, ContinuePolicyReportsEveryFailedResult) {
	std::vector<std::unique_ptr<camera::IFrameSource>> sources;
	sources.push_back(std::make_unique<ScriptedSource>(std::vector<std::uint64_t>{1, 2, 3}));
	runtime::MultiCameraExecutionOptions<int> options;
	std::size_t errors = 0;
	options.resultObserver = [&errors](auto, const auto&, auto, const auto& result) {
		EXPECT_FALSE(result);
		++errors;
	};
	runtime::MultiCameraSession<int> session(std::move(sources),
		std::make_unique<executor::SerialPipelineExecutor<int>>(
			std::make_unique<ContractPipeline>(true)), options);
	ASSERT_TRUE(session.start());
	const auto summary = session.wait();
	EXPECT_EQ(errors, 3U);
	EXPECT_EQ(summary.failed, 3U);
}

TEST(ReleaseContractTest, DropPolicyReportsTheRejectedFrameIdentity) {
	auto pipeline = std::make_unique<BlockingPipeline>();
	auto* blocking = pipeline.get();
	std::vector<std::unique_ptr<camera::IFrameSource>> sources;
	sources.push_back(std::make_unique<ScriptedSource>(
		std::vector<std::uint64_t>{1, 2, 3}, false,
		[blocking] { EXPECT_TRUE(blocking->waitStarted()); }));
	executor::ExecutorOptions executorOptions;
	executorOptions.queueCapacity = 1;
	executorOptions.queueFullPolicy = executor::QueueFullPolicy::Drop;
	std::promise<void> dropped;
	auto future = dropped.get_future();
	runtime::MultiCameraExecutionOptions<int> options;
	options.resultObserver = [&dropped](auto outcome, const auto& identity,
		auto taskId, const auto& result) {
		if (outcome == executor::FrameOutcome::Dropped) {
			EXPECT_EQ(identity.metadata.sequenceNumber, 3U);
			EXPECT_EQ(identity.sourceId, 0U);
			EXPECT_GT(identity.executionId, 0U);
			EXPECT_FALSE(taskId);
			EXPECT_EQ(result.status().code(), StatusCode::QueueFull);
			dropped.set_value();
		}
	};
	runtime::MultiCameraSession<int> session(std::move(sources),
		std::make_unique<executor::SerialPipelineExecutor<int>>(
			std::move(pipeline), executorOptions), options);
	ASSERT_TRUE(session.start());
	const auto ready = future.wait_for(std::chrono::seconds(5));
	blocking->release();
	const auto summary = session.wait();
	EXPECT_EQ(ready, std::future_status::ready);
	EXPECT_EQ(summary.dropped, 1U);
	EXPECT_EQ(summary.completed, 2U);
}

TEST(ReleaseContractTest, BatchMixedFailuresStillCompleteInSubmissionOrder) {
	executor::ExecutorOptions options;
	options.queueCapacity = 32;
	options.queueFullPolicy = executor::QueueFullPolicy::Block;
	std::vector<executor::TaskId> delivered;
	executor::BatchPipelineExecutor<int> executor(
		std::make_unique<ContractPipeline>(false, false, true),
		options, {.maxBatchSize = 4, .flushTimeout = std::chrono::milliseconds(20)});
	std::vector<executor::TaskHandle<int>> handles;
	for (std::uint64_t sequence = 1; sequence <= 100; ++sequence) {
		auto submitted = executor.submit(packet(sequence),
			[&delivered](auto id, const auto&) { delivered.push_back(id); });
		ASSERT_TRUE(submitted);
		handles.push_back(std::move(submitted).value());
	}

	executor.requestStop();
	executor.wait();
	ASSERT_EQ(delivered.size(), 100U);
	for (std::size_t index = 0; index < handles.size(); ++index) {
		EXPECT_EQ(delivered[index], handles[index].id());
		EXPECT_EQ(static_cast<bool>(handles[index].future().get()), index % 2 == 0);
	}
}

TEST(ReleaseContractTest, StopPolicyStopsTheSourceWhenInferenceFails) {
	auto source = std::make_unique<UntilStoppedSource>();
	auto* raw = source.get();
	std::vector<std::unique_ptr<camera::IFrameSource>> sources;
	sources.push_back(std::move(source));
	runtime::MultiCameraExecutionOptions<int> options;
	options.faultPolicy = executor::FaultPolicy::StopSession;
	runtime::MultiCameraSession<int> session(std::move(sources),
		std::make_unique<executor::SerialPipelineExecutor<int>>(
			std::make_unique<ContractPipeline>(true)), options);
	ASSERT_TRUE(session.start());
	const auto stoppedByPolicy = raw->waitStopped(std::chrono::seconds(5));
	session.requestStop();
	const auto summary = session.wait();
	EXPECT_EQ(stoppedByPolicy, std::future_status::ready);
	EXPECT_EQ(summary.failed, 1U);
}

TEST(ReleaseContractTest, GracefulStopDrainsTheInFlightSourceCallback) {
	auto pipeline = std::make_unique<BlockingPipeline>();
	auto* blocking = pipeline.get();
	std::vector<std::unique_ptr<camera::IFrameSource>> sources;
	sources.push_back(std::make_unique<ScriptedSource>(
		std::vector<std::uint64_t>{1, 2, 3}, false,
		[blocking] { EXPECT_TRUE(blocking->waitStarted()); }));
	executor::ExecutorOptions executorOptions;
	executorOptions.queueCapacity = 1;
	executorOptions.queueFullPolicy = executor::QueueFullPolicy::Block;
	std::promise<void> thirdFrame;
	auto ready = thirdFrame.get_future();
	runtime::MultiCameraExecutionOptions<int> options;
	options.frameObserver = [&thirdFrame](auto, const auto& frame) {
		if (frame.metadata().sequenceNumber == 3) {
			thirdFrame.set_value();
		}
	};
	runtime::MultiCameraSession<int> session(std::move(sources),
		std::make_unique<executor::SerialPipelineExecutor<int>>(
			std::move(pipeline), executorOptions), options);
	ASSERT_TRUE(session.start());
	const auto observed = ready.wait_for(std::chrono::seconds(5));
	session.requestStop();
	blocking->release();
	const auto summary = session.wait();
	EXPECT_EQ(observed, std::future_status::ready);
	EXPECT_EQ(summary.submitted, 3U);
	EXPECT_EQ(summary.completed, 3U);
	EXPECT_EQ(summary.rejected, 0U);
	EXPECT_EQ(summary.failed, 0U);
}
