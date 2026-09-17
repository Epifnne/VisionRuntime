#pragma once

#include "core/result.hpp"
#include "pipeline/inferenceOutput.hpp"
#include "pipeline/iVisionPipeline.hpp"
#include "pipeline/pipelinePacket.hpp"
#include "preProcess/preparedInput.hpp"

#include <cstdint>
#include <vector>

namespace visionRuntime::pipeline {

template<typename ResultType>
class IStagedVisionPipeline : public IVisionPipeline<ResultType> {
public:
	virtual ~IStagedVisionPipeline() = default;

	[[nodiscard]] virtual core::Result<preprocess::PreparedInput> preprocess(
		PipelinePacket packet) = 0;
	[[nodiscard]] virtual core::Result<InferenceOutput> infer(
		preprocess::PreparedInput input) = 0;
	[[nodiscard]] virtual core::Result<ResultType> postprocess(
		InferenceOutput output) = 0;

	/// Runs one backend inference for a batch of prepared inputs and returns
	/// one InferenceOutput per input, in the same order. Batch executors use
	/// this instead of touching the backend directly, so pipeline decorators
	/// (timing, logging) observe batched inference like any other stage.
	[[nodiscard]] virtual core::Result<std::vector<InferenceOutput>> inferBatch(
		std::vector<preprocess::PreparedInput> inputs) = 0;

	virtual void finishExecution(
		std::uint64_t executionId,
		const core::Result<ResultType>& result) noexcept {
		static_cast<void>(executionId);
		static_cast<void>(result);
	}
};

} // namespace visionRuntime::pipeline