#pragma once

#include "pipeline/iOpenCvAlgorithm.hpp"
#include "pipeline/iVisionPipeline.hpp"

#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace visionRuntime::pipeline {

template<typename ResultType>
class OpenCvPipelineBuilder;

template<typename ResultType>
class OpenCvPipeline final : public IVisionPipeline<ResultType> {
public:
	OpenCvPipeline(const OpenCvPipeline&) = delete;
	OpenCvPipeline& operator=(const OpenCvPipeline&) = delete;
	OpenCvPipeline(OpenCvPipeline&&) noexcept = default;
	OpenCvPipeline& operator=(OpenCvPipeline&&) noexcept = default;

	[[nodiscard]] core::Result<ResultType> run(PipelinePacket packet) override {
		if (!algorithm_) {
			return core::Result<ResultType>::failure(core::Status::error(
				core::StatusCode::InvalidState,
				"OpenCV pipeline has no algorithm"));
		}

		try {
			auto result = algorithm_->process(packet);
			packet.finishPostprocess();
			if (!result) {
				return core::Result<ResultType>::failure(
					result.status().withContext("opencv"));
			}
			return result;
		} catch (const std::exception& exception) {
			packet.finishPostprocess();
			return exceptionFailure(exception.what());
		} catch (...) {
			packet.finishPostprocess();
			return exceptionFailure("unknown exception");
		}
	}

private:
	friend class OpenCvPipelineBuilder<ResultType>;

	explicit OpenCvPipeline(std::unique_ptr<IOpenCvAlgorithm<ResultType>> algorithm)
		: algorithm_(std::move(algorithm)) {}

	[[nodiscard]] static core::Result<ResultType> exceptionFailure(
		const char* message) {
		return core::Result<ResultType>::failure(core::Status::error(
			core::StatusCode::Internal,
			std::string("opencv stage threw an exception: ") + message));
	}

	std::unique_ptr<IOpenCvAlgorithm<ResultType>> algorithm_;
};

} // namespace visionRuntime::pipeline