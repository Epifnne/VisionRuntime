#include "benchmark/anomalyCsvTimedPipeline.hpp"
#include "pipeline/pipelineBuilder.hpp"
#include "preprocess/preprocessChain.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using TensorMap = visionRuntime::preprocess::TensorMap;

struct InvocationPayload {
	int value;
};

class ReleasingPreprocessor final : public visionRuntime::preprocess::IPreprocessor {
public:
	visionRuntime::core::Result<visionRuntime::preprocess::PreparedInput> process(
		visionRuntime::pipeline::PipelinePacket packet) override {
		packet.completeImagePreparation();
		return visionRuntime::core::Result<visionRuntime::preprocess::PreparedInput>::success(
			visionRuntime::preprocess::PreparedInput(std::move(packet), {}));
	}
};

class PayloadPostprocessor final : public visionRuntime::postprocess::IPostprocessor<int> {
public:
	visionRuntime::core::Result<int> process(
		const TensorMap&,
		const visionRuntime::vision::TransformContext&,
		const visionRuntime::pipeline::PipelinePacket& packet) override {
		const auto* payload = packet.payload<InvocationPayload>();
		if (!payload || packet.hasCameraFrame()) {
			return visionRuntime::core::Result<int>::failure(
				visionRuntime::core::Status::error(
					visionRuntime::core::StatusCode::InvalidState,
					"payload missing or camera frame not released"));
		}
		return visionRuntime::core::Result<int>::success(payload->value);
	}
};

class RecordingPreprocessor final : public visionRuntime::preprocess::IPreprocessor {
public:
	explicit RecordingPreprocessor(std::vector<std::string>& calls) : calls_(calls) {}

	visionRuntime::core::Result<visionRuntime::preprocess::PreparedInput> process(
		visionRuntime::pipeline::PipelinePacket packet) override {
		calls_.push_back("preprocess");
		return visionRuntime::core::Result<visionRuntime::preprocess::PreparedInput>::success(
			visionRuntime::preprocess::PreparedInput(std::move(packet), {}));
	}

private:
	std::vector<std::string>& calls_;
};

class RecordingBackend final : public visionRuntime::backends::IInferenceBackend {
public:
	explicit RecordingBackend(std::vector<std::string>& calls) : calls_(calls) {}

	visionRuntime::core::Result<TensorMap> infer(const TensorMap&) override {
		calls_.push_back("inference");
		return visionRuntime::core::Result<TensorMap>::success({});
	}

private:
	std::vector<std::string>& calls_;
};

class RecordingPostprocessor final
	: public visionRuntime::postprocess::IPostprocessor<int> {
public:
	explicit RecordingPostprocessor(std::vector<std::string>& calls) : calls_(calls) {}

	visionRuntime::core::Result<int> process(
		const TensorMap&,
		const visionRuntime::vision::TransformContext&,
		const visionRuntime::pipeline::PipelinePacket&) override {
		calls_.push_back("postprocess");
		return visionRuntime::core::Result<int>::success(42);
	}

private:
	std::vector<std::string>& calls_;
};

class FailingBackend final : public visionRuntime::backends::IInferenceBackend {
public:
	visionRuntime::core::Result<TensorMap> infer(const TensorMap&) override {
		return visionRuntime::core::Result<TensorMap>::failure(
			visionRuntime::core::Status::error(
				visionRuntime::core::StatusCode::BackendError, "model execution failed"));
	}
};

class ThrowingPostprocessor final
	: public visionRuntime::postprocess::IPostprocessor<int> {
public:
	visionRuntime::core::Result<int> process(
		const TensorMap&,
		const visionRuntime::vision::TransformContext&,
		const visionRuntime::pipeline::PipelinePacket&) override {
		throw std::runtime_error("decoder failure");
	}
};

class AnomalyPostprocessor final
	: public visionRuntime::postprocess::IPostprocessor<
		visionRuntime::vision::AnomalyResult> {
public:
	visionRuntime::core::Result<visionRuntime::vision::AnomalyResult> process(
		const TensorMap&,
		const visionRuntime::vision::TransformContext&,
		const visionRuntime::pipeline::PipelinePacket&) override {
		return visionRuntime::core::Result<
			visionRuntime::vision::AnomalyResult>::success({
				.score = 2.5F,
				.threshold = 2.0F,
				.decision = visionRuntime::vision::AnomalyDecision::Ng});
	}
};

class SleepingNode final : public visionRuntime::preprocess::IPreprocessNode {
public:
	explicit SleepingNode(int delayMilliseconds, int failureMode = 0)
		: delayMilliseconds_(delayMilliseconds), failureMode_(failureMode) {}

	visionRuntime::core::Result<void> process(
		visionRuntime::preprocess::PreprocessContext&) override {
		std::this_thread::sleep_for(std::chrono::milliseconds(delayMilliseconds_));
		if (failureMode_ == 2) {
			throw std::runtime_error("node exception");
		}
		if (failureMode_ == 3) {
			throw 42;
		}
		if (failureMode_ == 1) {
			return visionRuntime::core::Result<void>::failure(
				visionRuntime::core::Status::error(
					visionRuntime::core::StatusCode::InvalidArgument, "node failure"));
		}
		return visionRuntime::core::Result<void>::success();
	}

private:
	int delayMilliseconds_;
	int failureMode_;
};

std::unique_ptr<visionRuntime::preprocess::IPreprocessor> makeSleepingChain(
	int failureMode = 0) {
	std::vector<std::unique_ptr<visionRuntime::preprocess::IPreprocessNode>> nodes;
	nodes.push_back(std::make_unique<SleepingNode>(2));
	nodes.push_back(std::make_unique<SleepingNode>(4, failureMode));
	nodes.push_back(std::make_unique<SleepingNode>(1));
	return std::make_unique<visionRuntime::preprocess::PreprocessChain>(std::move(nodes));
}

visionRuntime::pipeline::PipelineBuilder<int> makeBuilder(
	std::vector<std::string>& calls) {
	visionRuntime::pipeline::PipelineBuilder<int> builder;
	builder
		.setPreprocessor(std::make_unique<RecordingPreprocessor>(calls))
		.setBackend(std::make_unique<RecordingBackend>(calls))
		.setPostprocessor(std::make_unique<RecordingPostprocessor>(calls));
	return builder;
}

visionRuntime::pipeline::PipelineBuilder<visionRuntime::vision::AnomalyResult>
makeAnomalyBuilder(std::vector<std::string>& calls) {
	visionRuntime::pipeline::PipelineBuilder<visionRuntime::vision::AnomalyResult> builder;
	builder
		.setPreprocessor(std::make_unique<RecordingPreprocessor>(calls))
		.setBackend(std::make_unique<RecordingBackend>(calls))
		.setPostprocessor(std::make_unique<AnomalyPostprocessor>());
	return builder;
}

} // namespace

TEST(PipelineBuilderTest, RejectsMissingStages) {
	visionRuntime::pipeline::PipelineBuilder<int> builder;
	auto result = builder.build();

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visionRuntime::core::StatusCode::InvalidState);
}

TEST(PipelineTest, RunsLinearStagesInOrder) {
	std::vector<std::string> calls;
	auto pipelineResult = makeBuilder(calls).build();
	ASSERT_TRUE(pipelineResult);
	std::unique_ptr<visionRuntime::pipeline::IVisionPipeline<int>> pipeline =
		std::make_unique<visionRuntime::pipeline::ModelPipeline<int>>(
			std::move(pipelineResult).value());

	auto result = pipeline->run(visionRuntime::pipeline::PipelinePacket({}));

	ASSERT_TRUE(result);
	EXPECT_EQ(result.value(), 42);
	EXPECT_EQ(calls, (std::vector<std::string>{"preprocess", "inference", "postprocess"}));
}

TEST(PipelineTest, CarriesPayloadAcrossStagesAndBatchAfterFrameRelease) {
	using namespace visionRuntime;
	std::vector<std::string> calls;
	pipeline::PipelineBuilder<int> builder;
	builder
		.setPreprocessor(std::make_unique<ReleasingPreprocessor>())
		.setBackend(std::make_unique<RecordingBackend>(calls))
		.setPostprocessor(std::make_unique<PayloadPostprocessor>());
	auto built = builder.build();
	ASSERT_TRUE(built);

	pipeline::PipelinePacket packet({});
	packet.setPayload(InvocationPayload{42});
	auto result = built->run(std::move(packet));
	ASSERT_TRUE(result);
	EXPECT_EQ(result.value(), 42);

	std::vector<preprocess::PreparedInput> inputs;
	for (const int value : {11, 22}) {
		pipeline::PipelinePacket batchPacket({});
		batchPacket.setPayload(InvocationPayload{value});
		auto prepared = built->preprocess(std::move(batchPacket));
		ASSERT_TRUE(prepared);
		inputs.push_back(std::move(prepared).value());
	}
	auto outputs = built->inferBatch(std::move(inputs));
	ASSERT_TRUE(outputs);
	ASSERT_EQ(outputs->size(), 2);
	auto first = built->postprocess(std::move(outputs.value()[0]));
	auto second = built->postprocess(std::move(outputs.value()[1]));
	ASSERT_TRUE(first);
	ASSERT_TRUE(second);
	EXPECT_EQ(first.value(), 11);
	EXPECT_EQ(second.value(), 22);
}

TEST(PipelineTest, StopsAfterBackendFailureAndAddsStageContext) {
	std::vector<std::string> calls;
	visionRuntime::pipeline::PipelineBuilder<int> builder;
	builder
		.setPreprocessor(std::make_unique<RecordingPreprocessor>(calls))
		.setBackend(std::make_unique<FailingBackend>())
		.setPostprocessor(std::make_unique<RecordingPostprocessor>(calls));
	auto pipelineResult = builder.build();
	ASSERT_TRUE(pipelineResult);

	auto result = pipelineResult->run(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visionRuntime::core::StatusCode::BackendError);
	EXPECT_EQ(calls, (std::vector<std::string>{"preprocess"}));
	EXPECT_NE(result.status().toString().find("inference"), std::string::npos);
}

TEST(PipelineTest, ConvertsStageExceptionsToInternalStatus) {
	std::vector<std::string> calls;
	visionRuntime::pipeline::PipelineBuilder<int> builder;
	builder
		.setPreprocessor(std::make_unique<RecordingPreprocessor>(calls))
		.setBackend(std::make_unique<RecordingBackend>(calls))
		.setPostprocessor(std::make_unique<ThrowingPostprocessor>());
	auto pipelineResult = builder.build();
	ASSERT_TRUE(pipelineResult);

	auto result = pipelineResult->run(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(result);
	EXPECT_EQ(result.status().code(), visionRuntime::core::StatusCode::Internal);
	EXPECT_NE(result.status().message().find("decoder failure"), std::string::npos);
}

TEST(TimedPipelineTest, ReportsSuccessfulStageDurations) {
	std::vector<std::string> calls;
	auto pipelineResult = makeBuilder(calls).build();
	ASSERT_TRUE(pipelineResult);
	bool observed = false;
	visionRuntime::benchmark::TimedPipeline<int> pipeline(
		std::move(pipelineResult).value(),
		[&](std::uint64_t sequenceNumber,
			const visionRuntime::core::Result<int>& result,
			const visionRuntime::benchmark::PipelineDurations& durations) {
			observed = true;
			EXPECT_EQ(sequenceNumber, 0);
			ASSERT_TRUE(result);
			EXPECT_EQ(result.value(), 42);
			EXPECT_GE(durations.preprocessMilliseconds, 0.0);
			EXPECT_GE(durations.inferenceMilliseconds, 0.0);
			EXPECT_GE(durations.postprocessMilliseconds, 0.0);
			EXPECT_GE(durations.stageMilliseconds, 0.0);
			EXPECT_GE(durations.waitMilliseconds, 0.0);
			EXPECT_GE(durations.latencyMilliseconds,
				durations.stageMilliseconds);
			EXPECT_TRUE(durations.nodeTimings.empty());
		});

	auto result = pipeline.run(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_TRUE(result);
	EXPECT_TRUE(observed);
}

TEST(TimedPipelineTest, ReportsEachNodeIndependentlyAcrossRepeatedRuns) {
	using namespace visionRuntime;
	std::vector<std::string> calls;
	auto builder = makeBuilder(calls);
	builder.setPreprocessor(makeSleepingChain());
	auto built = builder.build();
	ASSERT_TRUE(built);
	int observed = 0;
	benchmark::TimedPipeline<int> timed(std::move(built).value(),
		[&](std::uint64_t, const core::Result<int>& result,
			const benchmark::PipelineDurations& durations) {
			++observed;
			ASSERT_TRUE(result);
			ASSERT_EQ(durations.nodeTimings.size(), 3);
			double total = 0;
			const int minimums[]{2, 4, 1};
			for (std::size_t index = 0; index < 3; ++index) {
				const auto& node = durations.nodeTimings[index];
				EXPECT_EQ(node.index, index);
				EXPECT_EQ(node.name, "CustomNode");
				EXPECT_TRUE(node.status.isOk());
				EXPECT_GE(node.milliseconds, minimums[index]);
				total += node.milliseconds;
			}
			EXPECT_LE(total, durations.preprocessMilliseconds);
			EXPECT_DOUBLE_EQ(durations.stageMilliseconds,
				durations.preprocessMilliseconds + durations.inferenceMilliseconds +
				durations.postprocessMilliseconds);
		});
	EXPECT_TRUE(timed.run(pipeline::PipelinePacket({})));
	EXPECT_TRUE(timed.run(pipeline::PipelinePacket({})));
	EXPECT_EQ(observed, 2);
}

TEST(TimedPipelineTest, IncludesFailedAndThrowingNodesButNotSkippedNodes) {
	using namespace visionRuntime;
	for (const int failureMode : {1, 2, 3}) {
		SCOPED_TRACE(failureMode);
		std::vector<std::string> calls;
		auto builder = makeBuilder(calls);
		builder.setPreprocessor(makeSleepingChain(failureMode));
		auto built = builder.build();
		ASSERT_TRUE(built);
		int observed = 0;
		benchmark::TimedPipeline<int> timed(std::move(built).value(),
			[&](std::uint64_t, const core::Result<int>& result,
				const benchmark::PipelineDurations& durations) {
				++observed;
				EXPECT_FALSE(result);
				ASSERT_EQ(durations.nodeTimings.size(), 2);
				EXPECT_TRUE(durations.nodeTimings[0].status.isOk());
				const auto& failed = durations.nodeTimings[1];
				EXPECT_EQ(failed.index, 1);
				EXPECT_GE(failed.milliseconds, 4);
				EXPECT_EQ(failed.status.code(), failureMode == 1
					? core::StatusCode::InvalidArgument : core::StatusCode::Internal);
				EXPECT_EQ(result.status().code(), failed.status.code());
				EXPECT_EQ(durations.inferenceMilliseconds, 0);
				EXPECT_EQ(durations.postprocessMilliseconds, 0);
			});
		// Exercise the executor path where failed preprocess has no later stages.
		EXPECT_FALSE(timed.preprocess(pipeline::PipelinePacket({})));
		EXPECT_TRUE(calls.empty());
		EXPECT_EQ(observed, 1);
		ASSERT_TRUE(timed.livePerformanceSnapshot());
		EXPECT_EQ(timed.livePerformanceSnapshot()->failed, 1);
	}
}

TEST(TimedPipelineTest, KeepsConcurrentPreprocessTimingsThroughBatchInference) {
	using namespace visionRuntime;
	std::vector<std::string> calls;
	auto builder = makeBuilder(calls);
	builder.setPreprocessor(makeSleepingChain());
	auto built = builder.build();
	ASSERT_TRUE(built);
	int observed = 0;
	benchmark::TimedPipeline<int> timed(std::move(built).value(),
		[&](std::uint64_t sequence, const core::Result<int>& result,
			const benchmark::PipelineDurations& durations) {
			++observed;
			EXPECT_EQ(sequence, 0);
			EXPECT_TRUE(result);
			ASSERT_EQ(durations.nodeTimings.size(), 3);
			EXPECT_EQ(durations.nodeTimings.front().index, 0);
			EXPECT_EQ(durations.nodeTimings.back().index, 2);
			EXPECT_GE(durations.nodeTimings[1].milliseconds, 4);
		});
	auto first = std::async(std::launch::async, [&] {
		return timed.preprocess(pipeline::PipelinePacket({}));
	});
	auto second = std::async(std::launch::async, [&] {
		return timed.preprocess(pipeline::PipelinePacket({}));
	});
	auto firstInput = first.get();
	auto secondInput = second.get();
	ASSERT_TRUE(firstInput);
	ASSERT_TRUE(secondInput);
	std::vector<preprocess::PreparedInput> inputs;
	inputs.push_back(std::move(firstInput).value());
	inputs.push_back(std::move(secondInput).value());
	auto outputs = timed.inferBatch(std::move(inputs));
	ASSERT_TRUE(outputs);
	ASSERT_EQ(outputs->size(), 2);
	for (auto& output : outputs.value()) {
		const auto executionId = output.packet().executionId();
		auto result = timed.postprocess(std::move(output));
		timed.finishExecution(executionId, result);
		timed.finishExecution(executionId, result);
	}
	EXPECT_EQ(observed, 2);
}

TEST(TimedPipelineTest, ReportsBackendFailure) {
	std::vector<std::string> calls;
	visionRuntime::pipeline::PipelineBuilder<int> builder;
	builder
		.setPreprocessor(makeSleepingChain())
		.setBackend(std::make_unique<FailingBackend>())
		.setPostprocessor(std::make_unique<RecordingPostprocessor>(calls));
	auto pipelineResult = builder.build();
	ASSERT_TRUE(pipelineResult);
	bool observed = false;
	visionRuntime::benchmark::TimedPipeline<int> pipeline(
		std::move(pipelineResult).value(),
		[&](std::uint64_t,
			const visionRuntime::core::Result<int>& result,
			const visionRuntime::benchmark::PipelineDurations& durations) {
			observed = true;
			EXPECT_FALSE(result);
			EXPECT_EQ(result.status().code(),
				visionRuntime::core::StatusCode::BackendError);
			ASSERT_EQ(durations.nodeTimings.size(), 3);
			for (const auto& node : durations.nodeTimings) {
				EXPECT_TRUE(node.status.isOk());
			}
		});

	auto result = pipeline.run(visionRuntime::pipeline::PipelinePacket({}));

	EXPECT_FALSE(result);
	EXPECT_TRUE(observed);
}

namespace {
class MonolithicPipeline final : public visionRuntime::pipeline::IVisionPipeline<int> {
public:
	explicit MonolithicPipeline(bool fail = false, bool throws = false) : fail_(fail), throws_(throws) {}
	visionRuntime::core::Result<int> run(visionRuntime::pipeline::PipelinePacket) override {
		if (throws_) { throw std::runtime_error("test exception"); }
		if (fail_) {
			return visionRuntime::core::Result<int>::failure(visionRuntime::core::Status::error(
				visionRuntime::core::StatusCode::Internal, "test failure"));
		}
		return visionRuntime::core::Result<int>::success(42);
	}
private:
	bool fail_;
	bool throws_;
};
}

TEST(TimedPipelineTest, ReportsMonolithicExecutionAndBatchOnce) {
	using namespace visionRuntime;
	for (const bool fail : {false, true}) {
		int observed = 0, batches = 0;
		benchmark::TimedPipeline<int> timed(std::make_unique<MonolithicPipeline>(fail),
			[&](std::uint64_t, const core::Result<int>& result,
				const benchmark::PipelineDurations& durations) {
				++observed;
				EXPECT_EQ(static_cast<bool>(result), !fail);
				EXPECT_EQ(durations.preprocessMilliseconds, 0);
				EXPECT_EQ(durations.inferenceMilliseconds, 0);
				EXPECT_GE(durations.stageMilliseconds, 0);
				EXPECT_EQ(durations.stageMilliseconds, durations.postprocessMilliseconds);
				EXPECT_GE(durations.latencyMilliseconds, durations.stageMilliseconds);
				EXPECT_TRUE(durations.nodeTimings.empty());
			}, [&](const benchmark::BatchPerformance& performance) {
				++batches;
				EXPECT_EQ(performance.completed, 1);
				EXPECT_EQ(performance.failed, fail ? 1 : 0);
			});
		EXPECT_EQ(static_cast<bool>(timed.run(pipeline::PipelinePacket({}))), !fail);
		timed.finishBatch();
		timed.finishBatch();
		EXPECT_EQ(observed, 1);
		EXPECT_EQ(batches, 1);
		ASSERT_TRUE(timed.livePerformanceSnapshot());
		EXPECT_EQ(timed.livePerformanceSnapshot()->stageSamples, 1);
	}
}

TEST(TimedPipelineTest, RejectsNullMonolithicPipeline) {
	using namespace visionRuntime;
	EXPECT_THROW((benchmark::TimedPipeline<int>(
		std::unique_ptr<pipeline::IVisionPipeline<int>>{}, {})), std::invalid_argument);
}

TEST(TimedPipelineTest, ReportsMonolithicExceptionAsFailure) {
	using namespace visionRuntime;
	bool observed = false;
	benchmark::TimedPipeline<int> timed(std::make_unique<MonolithicPipeline>(false, true),
		[&](std::uint64_t, const core::Result<int>& result, const benchmark::PipelineDurations&) {
			observed = true;
			ASSERT_FALSE(result);
			EXPECT_NE(result.status().message().find("test exception"), std::string::npos);
		});
	EXPECT_FALSE(timed.run(pipeline::PipelinePacket({})));
	EXPECT_TRUE(observed);
	ASSERT_TRUE(timed.livePerformanceSnapshot());
	EXPECT_EQ(timed.livePerformanceSnapshot()->failed, 1);
}

TEST(AnomalyCsvTimedPipelineTest, DoesNotOpenOutputWhenDeactivated) {
	std::vector<std::string> calls;
	auto pipelineResult = makeAnomalyBuilder(calls).build();
	ASSERT_TRUE(pipelineResult);
	visionRuntime::benchmark::AnomalyCsvTimingOptions options;
	options.outputPath = visionRuntime::benchmark::TimingOutputPath::file(
		std::filesystem::path{});

	auto timedPipeline = visionRuntime::benchmark::makeAnomalyCsvTimedPipeline(
		std::move(pipelineResult).value(), options);

	ASSERT_TRUE(timedPipeline);
	EXPECT_TRUE(timedPipeline.value()->run(
		visionRuntime::pipeline::PipelinePacket({})));
}

TEST(AnomalyCsvTimedPipelineTest, WritesHeaderAndResultToFile) {
	const auto outputPath = std::filesystem::temp_directory_path() /
		"visionRuntime-anomaly-timing.csv";
	std::filesystem::remove(outputPath);
	std::vector<std::string> calls;
	auto builder = makeAnomalyBuilder(calls);
	builder.setPreprocessor(makeSleepingChain());
	auto pipelineResult = builder.build();
	ASSERT_TRUE(pipelineResult);
	visionRuntime::benchmark::AnomalyCsvTimingOptions options;
	options.activate = true;
	options.outputPath =
		visionRuntime::benchmark::TimingOutputPath::file(outputPath);
	{
		auto timedPipeline = visionRuntime::benchmark::makeAnomalyCsvTimedPipeline(
			std::move(pipelineResult).value(), options);
		ASSERT_TRUE(timedPipeline);
		EXPECT_TRUE(timedPipeline.value()->run(
			visionRuntime::pipeline::PipelinePacket({})));
		timedPipeline.value()->finishBatch();
	}

	{
		std::ifstream output(outputPath);
		const std::string contents{
			std::istreambuf_iterator<char>(output), std::istreambuf_iterator<char>()};
		EXPECT_NE(contents.find("sequence,score,threshold,result,pre_ms,"),
			std::string::npos);
		EXPECT_NE(contents.find("stage_ms,wait_ms,latency_ms"),
			std::string::npos);
		EXPECT_NE(contents.find("latency_ms,node_timings\n"), std::string::npos);
		EXPECT_NE(contents.find("0,2.5,2,NG,"), std::string::npos);
		EXPECT_NE(contents.find("\"0:CustomNode="), std::string::npos);
		EXPECT_NE(contents.find("; 1:CustomNode="), std::string::npos);
		EXPECT_NE(contents.find("; 2:CustomNode="), std::string::npos);
		EXPECT_NE(contents.find("stage_p50_ms="), std::string::npos);
		EXPECT_NE(contents.find("stage_p95_ms="), std::string::npos);
		EXPECT_NE(contents.find("stage_p99_ms="), std::string::npos);
	}
	std::filesystem::remove(outputPath);
}

TEST(AnomalyCsvTimedPipelineTest, EscapesNodeDetailsAndWritesFailures) {
	using namespace visionRuntime;
	const auto outputPath = std::filesystem::temp_directory_path() /
		"visionRuntime-node-timing.csv";
	benchmark::PipelineDurations durations;
	durations.nodeTimings.push_back({0, "custom,\"quoted\"", 1.25,
		core::Status::error(core::StatusCode::InvalidArgument, "bad node")});
	{
		auto output = benchmark::detail::AnomalyCsvTimingOutput::create(
			benchmark::TimingOutputPath::file(outputPath));
		ASSERT_TRUE(output);
		output.value()->write(7, core::Result<vision::AnomalyResult>::failure(
			core::Status::error(core::StatusCode::InvalidArgument, "bad node")), durations);
	}
	{
		std::ifstream output(outputPath);
		const std::string contents{
			std::istreambuf_iterator<char>(output), std::istreambuf_iterator<char>()};
		EXPECT_NE(contents.find("7,,,ERROR,"), std::string::npos);
		EXPECT_NE(contents.find(
			"\"0:custom,\"\"quoted\"\"=1.250 ms [InvalidArgument: bad node]\"\n"),
			std::string::npos);
	}
	std::filesystem::remove(outputPath);
}

TEST(AnomalyCsvTimedPipelineTest, WritesReadableNodeDetails) {
	using namespace visionRuntime;
	auto output = benchmark::detail::AnomalyCsvTimingOutput::create(
		benchmark::TimingOutputPath::standardOutput());
	ASSERT_TRUE(output);
	benchmark::PipelineDurations durations;
	durations.nodeTimings.push_back({0, "CvResizeNode", 1.25, core::Status::ok()});
	durations.nodeTimings.push_back({1, "CustomNode", 2.5, core::Status::ok()});
	testing::internal::CaptureStdout();
	output.value()->write(9, core::Result<vision::AnomalyResult>::success({}), durations);
	const auto text = testing::internal::GetCapturedStdout();
	EXPECT_NE(text.find("nodes: 0:CvResizeNode=1.250 ms [Ok]; 1:CustomNode=2.500 ms [Ok]"),
		std::string::npos);
}