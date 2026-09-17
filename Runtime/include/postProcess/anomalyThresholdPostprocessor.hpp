#pragma once

#include "postProcess/iPostProcessor.hpp"
#include "vision/anomalyResult.hpp"

#include <memory>
#include <string>

namespace visionRuntime::postprocess {

struct AnomalyThresholdPostprocessorOptions {
	std::string outputName = "score";
	float threshold = 0.5F;
};

class AnomalyThresholdPostprocessor final
	: public IPostprocessor<vision::AnomalyResult> {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<AnomalyThresholdPostprocessor>> create(
		AnomalyThresholdPostprocessorOptions options);

	[[nodiscard]] core::Result<vision::AnomalyResult> process(
		const preprocess::TensorMap& outputs,
		const vision::TransformContext& transformContext,
		const pipeline::PipelinePacket& packet) override;

private:
	explicit AnomalyThresholdPostprocessor(AnomalyThresholdPostprocessorOptions options);

	AnomalyThresholdPostprocessorOptions options_;
};

} // namespace visionRuntime::postprocess