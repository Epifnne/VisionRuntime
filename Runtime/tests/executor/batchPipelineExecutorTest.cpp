#include "backends/iInferenceBackend.hpp"
#include "executor/batchPipelineExecutor.hpp"
#include "memory/cpuAllocator.hpp"
#include "pipeline/batchTensors.hpp"
#include "pipeline/iStagedVisionPipeline.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <latch>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {

using visionRuntime::core::DataType;
using visionRuntime::core::Result;
using visionRuntime::core::Tensor;
using visionRuntime::core::TensorShape;
using visionRuntime::pipeline::InferenceOutput;
using visionRuntime::pipeline::PipelinePacket;
using visionRuntime::preprocess::PreparedInput;
using visionRuntime::preprocess::TensorMap;

class FakeBatchBackend final : public visionRuntime::backends::IInferenceBackend {
public:
	Result<TensorMap> infer(const TensorMap& inputs) override {
		const auto& input = inputs.at("input");
		const auto batch = input.shape().dimensions()[0];
		const auto sampleFloats = 4;
		const auto* inputData = static_cast<const float*>(input.data());

		const auto totalFloats = static_cast<std::size_t>(batch) * sampleFloats;
		auto storage = std::shared_ptr<float[]>(new float[totalFloats]);
		for (std::int64_t index = 0; index < batch; ++index) {
			storage[static_cast<std::size_t>(index) * sampleFloats] =
				inputData[index * sampleFloats] * 2.0F;
		}
		auto output = Tensor::share(
			std::move(storage), totalFloats * sizeof(float), DataType::Float32,
			TensorShape({batch, sampleFloats}));
		if (!output) {
			return Result<TensorMap>::failure(output.status());
		}
		std::lock_guard lock(mutex_);
		++inferCalls_;
		lastBatchSize_ = batch;
		return Result<TensorMap>::success({{"output", std::move(output).value()}});
	}

	[[nodiscard]] std::size_t inferCalls() const {
		std::lock_guard lock(mutex_);
		return inferCalls_;
	}

	[[nodiscard]] std::int64_t lastBatchSize() const {
		std::lock_guard lock(mutex_);
		return lastBatchSize_;
	}

private:
	mutable std::mutex mutex_;
	std::size_t inferCalls_ = 0;
	std::int64_t lastBatchSize_ = 0;
};

class FakeBatchPipeline final
	: public visionRuntime::pipeline::IStagedVisionPipeline<float> {
public:
	Result<PreparedInput> preprocess(PipelinePacket packet) override {
		const auto value = static_cast<float>(++preprocessed_);
		auto storage = std::shared_ptr<float[]>(new float[4]);
		std::fill_n(storage.get(), 4, value);
		auto tensor = Tensor::share(
			std::move(storage), 4 * sizeof(float), DataType::Float32,
			TensorShape({1, 4}));
		if (!tensor) {
			return Result<PreparedInput>::failure(tensor.status());
		}
		return Result<PreparedInput>::success({
			std::move(packet), {{"input", std::move(tensor).value()}}});
	}

	Result<InferenceOutput> infer(PreparedInput) override {
		throw std::logic_error("per-frame infer must not be called by batch executor");
	}

	Result<std::vector<InferenceOutput>> inferBatch(
		std::vector<PreparedInput> inputs) override {
		auto batch = visionRuntime::pipeline::concatBatchTensors(inputs);
		if (!batch) {
			return Result<std::vector<InferenceOutput>>::failure(batch.status());
		}
		const auto batchSize = inputs.size();
		auto outputs = backend_.infer(batch.value());
		if (!outputs) {
			return Result<std::vector<InferenceOutput>>::failure(outputs.status());
		}
		auto perSample = visionRuntime::pipeline::splitBatchOutputs(
			outputs.value(), batchSize);
		if (!perSample) {
			return Result<std::vector<InferenceOutput>>::failure(
				perSample.status());
		}
		std::vector<InferenceOutput> results;
		results.reserve(batchSize);
		for (std::size_t index = 0; index < batchSize; ++index) {
			results.emplace_back(
				std::move(inputs[index].packet()),
				std::move(perSample.value()[index]),
				inputs[index].transformContext());
		}
		return Result<std::vector<InferenceOutput>>::success(std::move(results));
	}

	Result<float> postprocess(InferenceOutput output) override {
		const auto& tensor = output.tensors().at("output");
		return Result<float>::success(static_cast<const float*>(tensor.data())[0]);
	}

	Result<float> run(PipelinePacket packet) override {
		auto prepared = preprocess(std::move(packet));
		if (!prepared) {
			return Result<float>::failure(prepared.status());
		}
		return Result<float>::failure(visionRuntime::core::Status::error(
			visionRuntime::core::StatusCode::Internal, "use stages"));
	}

	FakeBatchBackend backend_;
	std::atomic_int preprocessed_{0};
};

visionRuntime::pipeline::PipelinePacket makePacket() {
	auto buffer = visionRuntime::memory::CpuAllocator{}.allocate(1);
	auto frame = visionRuntime::vision::Frame::create(
		std::move(buffer).value(), 1, 1,
		visionRuntime::vision::PixelFormat::Gray8);
	return visionRuntime::pipeline::PipelinePacket(std::move(frame).value());
}

visionRuntime::executor::ExecutorOptions executorOptions() {
	return {
		.queueCapacity = 64,
		.queueFullPolicy = visionRuntime::executor::QueueFullPolicy::Block,
		.stageQueueCapacity = 16,
	};
}

} // namespace

TEST(BatchPipelineExecutorTest, BatchesFullGroupIntoSingleInference) {
	auto pipeline = std::make_unique<FakeBatchPipeline>();
	auto* backend = &pipeline->backend_;
	visionRuntime::executor::BatchPipelineExecutor<float> executor(
		std::move(pipeline), executorOptions(), {.maxBatchSize = 3});

	std::vector<std::shared_future<Result<float>>> futures;
	for (int index = 0; index < 3; ++index) {
		auto handle = executor.submit(makePacket());
		ASSERT_TRUE(handle) << handle.status().toString();
		futures.push_back(handle->future());
	}
	for (std::size_t index = 0; index < futures.size(); ++index) {
		auto result = futures[index].get();
		ASSERT_TRUE(result) << result.status().toString();
		// member i holds preprocessed value (i + 1), backend doubles it
		EXPECT_FLOAT_EQ(result.value(), static_cast<float>(index + 1) * 2.0F);
	}
	EXPECT_EQ(backend->inferCalls(), 1U);
	EXPECT_EQ(backend->lastBatchSize(), 3);

	executor.requestStop();
	executor.wait();
}

TEST(BatchPipelineExecutorTest, FlushesPartialBatchAfterTimeout) {
	auto pipeline = std::make_unique<FakeBatchPipeline>();
	auto* backend = &pipeline->backend_;
	visionRuntime::executor::BatchPipelineExecutor<float> executor(
		std::move(pipeline), executorOptions(),
		{.maxBatchSize = 4, .flushTimeout = std::chrono::milliseconds(30)});

	auto first = executor.submit(makePacket());
	ASSERT_TRUE(first);
	auto second = executor.submit(makePacket());
	ASSERT_TRUE(second);

	auto firstResult = first->future().get();
	auto secondResult = second->future().get();
	ASSERT_TRUE(firstResult);
	ASSERT_TRUE(secondResult);
	EXPECT_FLOAT_EQ(firstResult.value(), 2.0F);
	EXPECT_FLOAT_EQ(secondResult.value(), 4.0F);
	EXPECT_EQ(backend->inferCalls(), 1U);
	EXPECT_EQ(backend->lastBatchSize(), 2);

	executor.requestStop();
	executor.wait();
}

TEST(BatchPipelineExecutorTest, SplitsBackToBackFullBatches) {
	auto pipeline = std::make_unique<FakeBatchPipeline>();
	auto* backend = &pipeline->backend_;
	visionRuntime::executor::BatchPipelineExecutor<float> executor(
		std::move(pipeline), executorOptions(),
		{.maxBatchSize = 2, .flushTimeout = std::chrono::milliseconds(500)});

	std::vector<std::shared_future<Result<float>>> futures;
	for (int index = 0; index < 4; ++index) {
		auto handle = executor.submit(makePacket());
		ASSERT_TRUE(handle);
		futures.push_back(handle->future());
	}
	for (std::size_t index = 0; index < futures.size(); ++index) {
		auto result = futures[index].get();
		ASSERT_TRUE(result);
		EXPECT_FLOAT_EQ(result.value(), static_cast<float>(index + 1) * 2.0F);
	}
	EXPECT_EQ(backend->inferCalls(), 2U);

	executor.requestStop();
	executor.wait();
}

TEST(BatchPipelineExecutorTest, GracefulStopFlushesPendingBatch) {
	auto pipeline = std::make_unique<FakeBatchPipeline>();
	auto* backend = &pipeline->backend_;
	visionRuntime::executor::BatchPipelineExecutor<float> executor(
		std::move(pipeline), executorOptions(),
		{.maxBatchSize = 8, .flushTimeout = std::chrono::seconds(60)});

	auto handle = executor.submit(makePacket());
	ASSERT_TRUE(handle);
	executor.requestStop();
	executor.wait();

	auto result = handle->future().get();
	ASSERT_TRUE(result) << result.status().toString();
	EXPECT_FLOAT_EQ(result.value(), 2.0F);
	EXPECT_EQ(backend->inferCalls(), 1U);
	EXPECT_EQ(backend->lastBatchSize(), 1);
}
