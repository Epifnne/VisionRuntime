#pragma once

#include "pipeline/openCvPipeline.hpp"

#include <memory>
#include <utility>

namespace visonRuntime::pipeline {

template<typename ResultType>
class OpenCvPipelineBuilder {
public:
	OpenCvPipelineBuilder& setAlgorithm(
		std::unique_ptr<IOpenCvAlgorithm<ResultType>> algorithm) noexcept {
		algorithm_ = std::move(algorithm);
		return *this;
	}

	[[nodiscard]] core::Result<OpenCvPipeline<ResultType>> build() {
		if (!algorithm_) {
			return core::Result<OpenCvPipeline<ResultType>>::failure(
				core::Status::error(
					core::StatusCode::InvalidState,
					"OpenCV pipeline requires an algorithm"));
		}

		return core::Result<OpenCvPipeline<ResultType>>::success(
			OpenCvPipeline<ResultType>(std::move(algorithm_)));
	}

private:
	std::unique_ptr<IOpenCvAlgorithm<ResultType>> algorithm_;
};

} // namespace visonRuntime::pipeline