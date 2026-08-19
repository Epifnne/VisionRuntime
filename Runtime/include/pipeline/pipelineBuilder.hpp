#pragma once

#include "pipeline/pipeline.hpp"

#include <memory>
#include <utility>

namespace visionRuntime::pipeline {

template<typename ResultType>
class PipelineBuilder {
public:
	PipelineBuilder& setPreprocessor(
		std::unique_ptr<preprocess::IPreprocessor> preprocessor) noexcept {
		preprocessor_ = std::move(preprocessor);
		return *this;
	}

	PipelineBuilder& setBackend(
		std::unique_ptr<backends::IInferenceBackend> backend) noexcept {
		backend_ = std::move(backend);
		return *this;
	}

	PipelineBuilder& setPostprocessor(
		std::unique_ptr<postprocess::IPostprocessor<ResultType>> postprocessor) noexcept {
		postprocessor_ = std::move(postprocessor);
		return *this;
	}

	[[nodiscard]] core::Result<Pipeline<ResultType>> build() {
		if (!preprocessor_) {
			return missingStage("pipeline requires a preprocessor");
		}
		if (!backend_) {
			return missingStage("pipeline requires a backend");
		}
		if (!postprocessor_) {
			return missingStage("pipeline requires a postprocessor");
		}

		return core::Result<Pipeline<ResultType>>::success(Pipeline<ResultType>(
			std::move(preprocessor_), std::move(backend_), std::move(postprocessor_)));
	}

private:
	[[nodiscard]] static core::Result<Pipeline<ResultType>> missingStage(
		const char* message) {
		return core::Result<Pipeline<ResultType>>::failure(core::Status::error(
			core::StatusCode::InvalidState, message));
	}

	std::unique_ptr<preprocess::IPreprocessor> preprocessor_;
	std::unique_ptr<backends::IInferenceBackend> backend_;
	std::unique_ptr<postprocess::IPostprocessor<ResultType>> postprocessor_;
};

template<typename ResultType>
using ModelPipelineBuilder = PipelineBuilder<ResultType>;

} // namespace visionRuntime::pipeline