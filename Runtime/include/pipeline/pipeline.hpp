#pragma once

#include "backends/iInferenceBackend.hpp"
#include "core/result.hpp"
#include "pipeline/batchTensors.hpp"
#include "pipeline/inferenceOutput.hpp"
#include "pipeline/iStagedVisionPipeline.hpp"
#include "pipeline/iVisionPipeline.hpp"
#include "postProcess/iPostProcessor.hpp"
#include "preProcess/iPreProcessor.hpp"

#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace visionRuntime::pipeline {

template<typename ResultType>
class PipelineBuilder;

template<typename ResultType>
class Pipeline final : public IStagedVisionPipeline<ResultType> {
public:
	Pipeline(const Pipeline&) = delete;
	Pipeline& operator=(const Pipeline&) = delete;
	Pipeline(Pipeline&&) noexcept = default;
	Pipeline& operator=(Pipeline&&) noexcept = default;

	[[nodiscard]] core::Result<preprocess::PreparedInput> preprocess(
		PipelinePacket packet) override {
		try {
			auto prepared = preprocessor_->process(std::move(packet));
			if (!prepared) {
				return core::Result<preprocess::PreparedInput>::failure(
					prepared.status().withContext("preprocess"));
			}
			return prepared;
		} catch (const std::exception& exception) {
			return exceptionFailure<preprocess::PreparedInput>("preprocess", exception.what());
		} catch (...) {
			return exceptionFailure<preprocess::PreparedInput>("preprocess", "unknown exception");
		}
	}

	[[nodiscard]] core::Result<InferenceOutput> infer(
		preprocess::PreparedInput input) override {
		try {
			auto outputs = backend_->infer(input.tensors());
			if (!outputs) {
				return core::Result<InferenceOutput>::failure(
					outputs.status().withContext("inference"));
			}
			return core::Result<InferenceOutput>::success(InferenceOutput(
				std::move(input.packet()), std::move(outputs).value(),
				input.transformContext()));
		} catch (const std::exception& exception) {
			return exceptionFailure<InferenceOutput>("inference", exception.what());
		} catch (...) {
			return exceptionFailure<InferenceOutput>("inference", "unknown exception");
		}
	}

	[[nodiscard]] core::Result<ResultType> postprocess(InferenceOutput output) override {
		try {
			auto result = postprocessor_->process(
				output.tensors(), output.transformContext(), output.packet());
			output.packet().finishPostprocess();
			if (!result) {
				return core::Result<ResultType>::failure(
					result.status().withContext("postprocess"));
			}
			return result;
		} catch (const std::exception& exception) {
			output.packet().finishPostprocess();
			return exceptionFailure<ResultType>("postprocess", exception.what());
		} catch (...) {
			output.packet().finishPostprocess();
			return exceptionFailure<ResultType>("postprocess", "unknown exception");
		}
	}

	[[nodiscard]] core::Result<ResultType> run(PipelinePacket packet) override {
		auto prepared = preprocess(std::move(packet));
		if (!prepared) {
			return core::Result<ResultType>::failure(prepared.status());
		}
		auto output = infer(std::move(prepared).value());
		if (!output) {
			return core::Result<ResultType>::failure(output.status());
		}
		return postprocess(std::move(output).value());
	}

	[[nodiscard]] core::Result<std::vector<InferenceOutput>> inferBatch(
		std::vector<preprocess::PreparedInput> inputs) override {
		try {
			auto batch = concatBatchTensors(inputs);
			if (!batch) {
				return core::Result<std::vector<InferenceOutput>>::failure(
					batch.status().withContext("inference"));
			}
			const auto batchSize = inputs.size();
			auto outputs = backend_->infer(batch.value());
			if (!outputs) {
				return core::Result<std::vector<InferenceOutput>>::failure(
					outputs.status().withContext("inference"));
			}
			auto perSample = splitBatchOutputs(outputs.value(), batchSize);
			if (!perSample) {
				return core::Result<std::vector<InferenceOutput>>::failure(
					perSample.status().withContext("inference"));
			}
			std::vector<InferenceOutput> results;
			results.reserve(batchSize);
			for (std::size_t index = 0; index < batchSize; ++index) {
				results.emplace_back(
					std::move(inputs[index].packet()),
					std::move(perSample.value()[index]),
					inputs[index].transformContext());
			}
			return core::Result<std::vector<InferenceOutput>>::success(
				std::move(results));
		} catch (const std::exception& exception) {
			return exceptionFailure<std::vector<InferenceOutput>>(
				"inference", exception.what());
		} catch (...) {
			return exceptionFailure<std::vector<InferenceOutput>>(
				"inference", "unknown exception");
		}
	}

private:
	friend class PipelineBuilder<ResultType>;

	Pipeline(
		std::unique_ptr<preprocess::IPreprocessor> preprocessor,
		std::unique_ptr<backends::IInferenceBackend> backend,
		std::unique_ptr<postprocess::IPostprocessor<ResultType>> postprocessor)
		: preprocessor_(std::move(preprocessor)),
		  backend_(std::move(backend)),
		  postprocessor_(std::move(postprocessor)) {}

	template<typename T>
	[[nodiscard]] static core::Result<T> exceptionFailure(
		const char* stage,
		const char* message) {
		return core::Result<T>::failure(core::Status::error(
			core::StatusCode::Internal,
			std::string(stage) + " stage threw an exception: " + message));
	}

	std::unique_ptr<preprocess::IPreprocessor> preprocessor_;
	std::unique_ptr<backends::IInferenceBackend> backend_;
	std::unique_ptr<postprocess::IPostprocessor<ResultType>> postprocessor_;
};

template<typename ResultType>
using ModelPipeline = Pipeline<ResultType>;

} // namespace visionRuntime::pipeline