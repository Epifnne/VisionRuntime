#include "executor/taskParallelPipelineExecutor.hpp"
#include "runtime/runtimeFactory.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace {
namespace vr = visionRuntime;
using Executor = vr::executor::TaskParallelPipelineExecutor<int>;
using Handle = vr::executor::TaskHandle<int>;

struct Control {
	std::mutex mutex;
	std::condition_variable changed;
	std::set<int> blocked;
	std::set<int> started;
	std::set<int> ended;
	int active = 0;
	int peak = 0;
	std::atomic_int created = 0;
	std::atomic_int destroyed = 0;
	std::atomic_int finished = 0;

	bool await(const std::function<bool()>& predicate) {
		std::unique_lock lock(mutex);
		return changed.wait_for(lock, std::chrono::seconds(5), predicate);
	}
	void release(int id) {
		std::lock_guard lock(mutex);
		blocked.erase(id);
		changed.notify_all();
	}
	void releaseAll() {
		std::lock_guard lock(mutex);
		blocked.clear();
		changed.notify_all();
	}
};

struct ReleaseOnExit {
	std::shared_ptr<Control> control;
	~ReleaseOnExit() { control->releaseAll(); }
};

class TestPipeline final : public vr::pipeline::IVisionPipeline<int> {
public:
	explicit TestPipeline(std::shared_ptr<Control> control) : control_(std::move(control)) {
		++control_->created;
	}
	~TestPipeline() override { ++control_->destroyed; }
	vr::core::Result<int> run(vr::pipeline::PipelinePacket packet) override {
		const int id = *packet.payload<int>();
		if (id < 0) throw std::runtime_error("injected pipeline failure");
		std::unique_lock lock(control_->mutex);
		++control_->active;
		control_->peak = std::max(control_->peak, control_->active);
		control_->started.insert(id);
		control_->changed.notify_all();
		control_->changed.wait(lock, [&] { return !control_->blocked.contains(id); });
		--control_->active;
		control_->ended.insert(id);
		control_->changed.notify_all();
		return vr::core::Result<int>::success(id);
	}
	void finishBatch() noexcept override { ++control_->finished; }
private:
	std::shared_ptr<Control> control_;
};

class ConstantPipeline final : public vr::pipeline::IVisionPipeline<int> {
public:
	vr::core::Result<int> run(vr::pipeline::PipelinePacket) override {
		return vr::core::Result<int>::success(7);
	}
};

class FiniteSource final : public vr::camera::IFrameSource {
public:
	vr::core::Result<void> start(vr::camera::FrameCallback callback) override {
		if (started_ || !callback) {
			return vr::core::Result<void>::failure(vr::core::Status::error(
				vr::core::StatusCode::InvalidState, "invalid source start"));
		}
		started_ = true;
		for (int i = 0; i < 6 && !stopped_; ++i) {
			callback(vr::core::Result<vr::vision::Frame>::success(vr::vision::Frame{}));
		}
		return vr::core::Result<void>::success();
	}
	void requestStop() noexcept override { stopped_ = true; }
	void wait() noexcept override {}
	bool isRunning() const noexcept override { return false; }
	vr::camera::FrameSourceInfo info() const override {
		return {.expectedFrameCount = 6, .isFinite = true};
	}
private:
	bool started_ = false;
	std::atomic_bool stopped_ = false;
};

vr::executor::PipelineFactory<int> factory(const std::shared_ptr<Control>& control) {
	return [control] {
		return vr::core::Result<std::unique_ptr<vr::pipeline::IVisionPipeline<int>>>::success(
			std::make_unique<TestPipeline>(control));
	};
}

vr::pipeline::PipelinePacket packet(int id) {
	vr::pipeline::PipelinePacket result({});
	result.setPayload(id);
	return result;
}
} // namespace

TEST(TaskParallelExecutorTest, OverlapsWholePipelinesAndDeliversInSubmissionOrder) {
	auto control = std::make_shared<Control>();
	control->blocked = {1, 2};
	auto built = Executor::create(factory(control), {}, {2, 4});
	ASSERT_TRUE(built);
	auto executor = std::move(built).value();
	ReleaseOnExit release{control};
	std::vector<int> callbacks;
	auto callback = [&](auto, const auto& result) { callbacks.push_back(result.value()); };
	auto first = executor->submit(packet(1), callback);
	auto second = executor->submit(packet(2), callback);
	ASSERT_TRUE(first);
	ASSERT_TRUE(second);
	ASSERT_TRUE(control->await([&] { return control->started.size() == 2; }));
	EXPECT_EQ(control->peak, 2);
	EXPECT_EQ(control->created, 2);
	control->release(2);
	ASSERT_TRUE(control->await([&] { return control->ended.contains(2); }));
	EXPECT_EQ(second->future().wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
	control->release(1);
	executor->wait();
	EXPECT_EQ(first->future().get().value(), 1);
	EXPECT_EQ(second->future().get().value(), 2);
	EXPECT_EQ(callbacks, (std::vector<int>{1, 2}));
	executor->wait();
	EXPECT_EQ(control->finished, 2);
}

TEST(TaskParallelExecutorTest, BoundsOutOfOrderResultsUntilDelivery) {
	auto control = std::make_shared<Control>();
	control->blocked = {1};
	auto built = Executor::create(factory(control), {}, {2, 2});
	ASSERT_TRUE(built);
	auto executor = std::move(built).value();
	ReleaseOnExit release{control};
	ASSERT_TRUE(executor->submit(packet(1)));
	auto second = executor->submit(packet(2));
	ASSERT_TRUE(second);
	ASSERT_TRUE(control->await([&] { return control->ended.contains(2); }));
	auto rejected = executor->submit(packet(3));
	ASSERT_FALSE(rejected);
	EXPECT_EQ(rejected.status().code(), vr::core::StatusCode::QueueFull);
	control->releaseAll();
	executor->wait();
}

TEST(TaskParallelExecutorTest, BoundsPendingQueueSeparately) {
	auto control = std::make_shared<Control>();
	control->blocked = {1};
	vr::executor::ExecutorOptions options;
	options.queueCapacity = 1;
	auto built = Executor::create(factory(control), options, {1, 4});
	ASSERT_TRUE(built);
	auto executor = std::move(built).value();
	ReleaseOnExit release{control};
	ASSERT_TRUE(executor->submit(packet(1)));
	ASSERT_TRUE(control->await([&] { return control->started.contains(1); }));
	ASSERT_TRUE(executor->submit(packet(2)));
	auto rejected = executor->submit(packet(3));
	ASSERT_FALSE(rejected);
	EXPECT_EQ(rejected.status().code(), vr::core::StatusCode::QueueFull);
	control->releaseAll();
	executor->wait();
}

TEST(TaskParallelExecutorTest, HoldsAdmissionUntilCallbackFinishesAndRejectsBlockingReentry) {
	auto control = std::make_shared<Control>();
	control->blocked = {99};
	vr::executor::ExecutorOptions options;
	options.queueFullPolicy = vr::executor::QueueFullPolicy::Block;
	auto built = Executor::create(factory(control), options, {1, 1});
	ASSERT_TRUE(built);
	auto executor = std::move(built).value();
	ReleaseOnExit release{control};
	std::promise<vr::core::StatusCode> reentry;
	auto first = executor->submit(packet(1), [&](auto, const auto&) {
		auto rejected = executor->submit(packet(2));
		reentry.set_value(rejected.status().code());
		std::unique_lock lock(control->mutex);
		control->started.insert(99);
		control->changed.notify_all();
		control->changed.wait(lock, [&] { return !control->blocked.contains(99); });
	});
	ASSERT_TRUE(first);
	ASSERT_TRUE(control->await([&] { return control->started.contains(99); }));
	EXPECT_EQ(reentry.get_future().get(), vr::core::StatusCode::InvalidState);
	auto waiting = std::async(std::launch::async, [&] { return executor->submit(packet(3)); });
	EXPECT_EQ(waiting.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
	control->release(99);
	ASSERT_TRUE(waiting.get());
	executor->wait();
}

TEST(TaskParallelExecutorTest, ImmediateStopCancelsRunningQueuedAndReadyTasks) {
	auto control = std::make_shared<Control>();
	control->blocked = {1};
	auto built = Executor::create(factory(control), {}, {2, 4});
	ASSERT_TRUE(built);
	auto executor = std::move(built).value();
	ReleaseOnExit release{control};
	std::vector<Handle> handles;
	for (int i = 1; i <= 3; ++i) {
		auto submitted = executor->submit(packet(i));
		ASSERT_TRUE(submitted);
		handles.push_back(std::move(submitted).value());
	}
	ASSERT_TRUE(control->await([&] { return control->ended.contains(2); }));
	executor->requestStop(vr::executor::StopMode::Immediate);
	control->releaseAll();
	executor->wait();
	for (const auto& handle : handles) {
		ASSERT_FALSE(handle.future().get());
		EXPECT_EQ(handle.future().get().status().code(), vr::core::StatusCode::Cancelled);
	}
	EXPECT_FALSE(executor->submit(packet(4)));
}

TEST(TaskParallelExecutorTest, HandleCancellationSkipsQueuedExecution) {
	auto control = std::make_shared<Control>();
	control->blocked = {1};
	auto built = Executor::create(factory(control), {}, {1, 3});
	ASSERT_TRUE(built);
	auto executor = std::move(built).value();
	ReleaseOnExit release{control};
	ASSERT_TRUE(executor->submit(packet(1)));
	ASSERT_TRUE(control->await([&] { return control->started.contains(1); }));
	auto queued = executor->submit(packet(2));
	ASSERT_TRUE(queued);
	EXPECT_TRUE(queued->cancel());
	control->releaseAll();
	executor->wait();
	EXPECT_EQ(queued->future().get().status().code(), vr::core::StatusCode::Cancelled);
	EXPECT_FALSE(control->started.contains(2));
}

TEST(TaskParallelExecutorTest, StopWakesBlockedSubmittersAndGracefullyDrainsAcceptedTasks) {
	auto control = std::make_shared<Control>();
	control->blocked = {1};
	vr::executor::ExecutorOptions options;
	options.queueFullPolicy = vr::executor::QueueFullPolicy::Block;
	auto built = Executor::create(factory(control), options, {1, 1});
	ASSERT_TRUE(built);
	auto executor = std::move(built).value();
	ReleaseOnExit release{control};
	auto first = executor->submit(packet(1));
	ASSERT_TRUE(first);
	ASSERT_TRUE(control->await([&] { return control->started.contains(1); }));
	auto waiting = std::async(std::launch::async, [&] { return executor->submit(packet(2)); });
	executor->requestStop();
	auto rejected = waiting.get();
	ASSERT_FALSE(rejected);
	EXPECT_EQ(rejected.status().code(), vr::core::StatusCode::InvalidState);
	control->releaseAll();
	executor->wait();
	EXPECT_EQ(first->future().get().value(), 1);
}

TEST(TaskParallelExecutorTest, DeadlineIncludesAdmissionAndOrderedDelivery) {
	auto control = std::make_shared<Control>();
	control->blocked = {1};
	vr::executor::ExecutorOptions options;
	options.queueFullPolicy = vr::executor::QueueFullPolicy::Block;
	options.taskTimeout = std::chrono::milliseconds(100);
	auto built = Executor::create(factory(control), options, {2, 2});
	ASSERT_TRUE(built);
	auto executor = std::move(built).value();
	ReleaseOnExit release{control};
	auto first = executor->submit(packet(1));
	auto second = executor->submit(packet(2));
	ASSERT_TRUE(first);
	ASSERT_TRUE(second);
	ASSERT_TRUE(control->await([&] { return control->ended.contains(2); }));
	auto rejected = executor->submit(packet(3));
	ASSERT_FALSE(rejected);
	EXPECT_EQ(rejected.status().code(), vr::core::StatusCode::DeadlineExceeded);
	control->releaseAll();
	executor->wait();
	EXPECT_EQ(first->future().get().status().code(), vr::core::StatusCode::DeadlineExceeded);
	EXPECT_EQ(second->future().get().status().code(), vr::core::StatusCode::DeadlineExceeded);
}

TEST(TaskParallelExecutorTest, ExceptionsDoNotLoseCompletionsOrStopWorkers) {
	auto control = std::make_shared<Control>();
	auto built = Executor::create(factory(control), {}, {2, 4});
	ASSERT_TRUE(built);
	auto executor = std::move(built).value();
	auto failed = executor->submit(packet(-1), [](auto, const auto&) {
		throw std::runtime_error("injected callback failure");
	});
	auto success = executor->submit(packet(2));
	ASSERT_TRUE(failed);
	ASSERT_TRUE(success);
	executor->wait();
	EXPECT_EQ(failed->future().get().status().code(), vr::core::StatusCode::Internal);
	EXPECT_EQ(success->future().get().value(), 2);
}

TEST(TaskParallelExecutorTest, ConcurrentProducersWrapBoundedSlotsWithoutLosingTasks) {
	auto control = std::make_shared<Control>();
	vr::executor::ExecutorOptions options;
	options.queueCapacity = 2;
	options.queueFullPolicy = vr::executor::QueueFullPolicy::Block;
	auto built = Executor::create(factory(control), options, {3, 5});
	ASSERT_TRUE(built);
	auto executor = std::move(built).value();
	std::vector<vr::executor::TaskId> delivered;
	std::vector<std::future<bool>> producers;
	for (int producer = 0; producer < 4; ++producer) {
		producers.push_back(std::async(std::launch::async, [&, producer] {
			for (int i = 0; i < 50; ++i) {
				auto submitted = executor->submit(packet(producer * 50 + i),
					[&](auto id, const auto&) { delivered.push_back(id); });
				if (!submitted || !submitted->future().get()) return false;
			}
			return true;
		}));
	}
	for (auto& producer : producers) EXPECT_TRUE(producer.get());
	executor->wait();
	ASSERT_EQ(delivered.size(), 200U);
	for (std::size_t i = 0; i < delivered.size(); ++i) EXPECT_EQ(delivered[i], i + 1);
}

TEST(TaskParallelExecutorTest, FactoryRejectsInvalidOptionsAndCleansUpPartialConstruction) {
	auto control = std::make_shared<Control>();
	EXPECT_FALSE(Executor::create({}, {}, {1, 1}));
	EXPECT_FALSE(Executor::create(factory(control), {}, {0, 1}));
	EXPECT_FALSE(Executor::create(factory(control), {}, {2, 1}));
	vr::executor::PipelineFactory<int> failing = [control] {
		using Pipeline = std::unique_ptr<vr::pipeline::IVisionPipeline<int>>;
		if (control->created == 1) {
			return vr::core::Result<Pipeline>::failure(vr::core::Status::error(
				vr::core::StatusCode::BackendError, "injected factory failure"));
		}
		return vr::core::Result<Pipeline>::success(std::make_unique<TestPipeline>(control));
	};
	auto failed = Executor::create(failing, {}, {2, 2});
	ASSERT_FALSE(failed);
	EXPECT_EQ(failed.status().code(), vr::core::StatusCode::BackendError);
	EXPECT_EQ(control->destroyed, 1);
	EXPECT_FALSE(Executor::create([]() -> vr::core::Result<
		std::unique_ptr<vr::pipeline::IVisionPipeline<int>>> {
		throw std::runtime_error("factory exception");
	}));
	EXPECT_FALSE(Executor::create([] {
		return vr::core::Result<std::unique_ptr<vr::pipeline::IVisionPipeline<int>>>::success(
			nullptr);
	}));
}

TEST(TaskParallelExecutorTest, RuntimeFactoryRequiresIndependentPipelineFactory) {
	auto control = std::make_shared<Control>();
	vr::config::DeploymentConfig config;
	config.executor.performancePolicy = vr::config::PerformancePolicy::TaskParallel;
	config.executor.workerCount = 2;
	config.executor.maxInFlight = 4;
	auto single = vr::runtime::RuntimeFactory::createExecutor<int>(
		std::make_unique<TestPipeline>(control), config);
	ASSERT_FALSE(single);
	auto built = vr::runtime::RuntimeFactory::createExecutor<int>(factory(control), config);
	ASSERT_TRUE(built);
	auto submitted = built.value()->submit(packet(42));
	ASSERT_TRUE(submitted);
	built.value()->wait();
	EXPECT_EQ(submitted->future().get().value(), 42);
	auto batch = vr::runtime::RuntimeFactory::createBatchExecutor<int>(nullptr, config, {});
	ASSERT_FALSE(batch);
	EXPECT_EQ(batch.status().code(), vr::core::StatusCode::InvalidArgument);
	EXPECT_FALSE(vr::runtime::RuntimeFactory::createExecutor<int>(nullptr, config));
	EXPECT_FALSE(vr::runtime::RuntimeFactory::createRuntime<int>(nullptr, nullptr, config));
}

TEST(TaskParallelExecutorTest, RuntimeSessionFactoryProcessesFiniteSource) {
	vr::config::DeploymentConfig config;
	config.executor.performancePolicy = vr::config::PerformancePolicy::TaskParallel;
	config.executor.workerCount = 2;
	config.executor.maxInFlight = 4;
	config.executor.queueFullPolicy = vr::config::QueueFullPolicy::Block;
	std::vector<int> values;
	auto runtime = vr::runtime::RuntimeFactory::createRuntime<int>(
		std::make_unique<FiniteSource>(), [] {
			return vr::core::Result<std::unique_ptr<vr::pipeline::IVisionPipeline<int>>>::success(
				std::make_unique<ConstantPipeline>());
		}, config, {.completionCallback = [&](auto, const auto& result) {
			values.push_back(result.value());
		}});
	ASSERT_TRUE(runtime);
	ASSERT_TRUE(runtime.value()->start());
	const auto summary = runtime.value()->wait();
	EXPECT_EQ(summary.received, 6U);
	EXPECT_EQ(summary.completed, 6U);
	EXPECT_EQ(summary.failed, 0U);
	EXPECT_EQ(summary.dropped, 0U);
	EXPECT_EQ(values, (std::vector<int>{7, 7, 7, 7, 7, 7}));
}

TEST(TaskParallelExecutorTest, CompletionCallbackCanRequestStop) {
	auto control = std::make_shared<Control>();
	auto built = Executor::create(factory(control));
	ASSERT_TRUE(built);
	auto executor = std::move(built).value();
	auto submitted = executor->submit(packet(1), [&](auto, const auto&) {
		executor->requestStop();
	});
	ASSERT_TRUE(submitted);
	executor->wait();
	EXPECT_EQ(submitted->future().get().value(), 1);
}
