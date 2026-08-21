#pragma once

#include "postProcess/iPostProcessor.hpp"
#include "vision/anomalyResult.hpp"

#include <memory>

namespace visionRuntime::postprocess {

struct AnomalyThresholdPostprocessorOptions {
	float threshold = 0.5F;
};

class AnomalyThresholdPostprocessor final
	: public IPostprocessor<vision::AnomalyResult> {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<AnomalyThresholdPostprocessor>> create(
		std::unique_ptr<IPostprocessor<vision::AnomalyResult>> scorePostprocessor,
		AnomalyThresholdPostprocessorOptions options);

	[[nodiscard]] core::Result<vision::AnomalyResult> process(
		const preprocess::TensorMap& outputs,
		const vision::TransformContext& transformContext,
		const pipeline::PipelinePacket& packet) override;

private:
	AnomalyThresholdPostprocessor(
		std::unique_ptr<IPostprocessor<vision::AnomalyResult>> scorePostprocessor,
		AnomalyThresholdPostprocessorOptions options);

	std::unique_ptr<IPostprocessor<vision::AnomalyResult>> scorePostprocessor_;
	AnomalyThresholdPostprocessorOptions options_;
};

} // namespace visionRuntime::postprocess