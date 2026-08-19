#pragma once

#include "postprocess/iPostprocessor.hpp"
#include "vision/anomalyResult.hpp"

#include <memory>
#include <string>

namespace visionRuntime::postprocess {

struct AnomalyPostprocessorOptions {
	std::string outputName;
};

class AnomalyPostprocessor final : public IPostprocessor<vision::AnomalyResult> {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<AnomalyPostprocessor>> create(
		AnomalyPostprocessorOptions options);

	[[nodiscard]] core::Result<vision::AnomalyResult> process(
		const preprocess::TensorMap& outputs,
		const vision::TransformContext& transformContext,
		const pipeline::PipelinePacket& packet) override;

private:
	explicit AnomalyPostprocessor(AnomalyPostprocessorOptions options);

	AnomalyPostprocessorOptions options_;
};

} // namespace visionRuntime::postprocess