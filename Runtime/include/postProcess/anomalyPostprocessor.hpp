#pragma once

#include "postProcess/iPostProcessor.hpp"
#include "vision/anomalyResult.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace faiss {
struct IndexFlatL2;
}

namespace visionRuntime::postprocess {

struct AnomalyPostprocessorOptions {
	std::string outputName;
	std::filesystem::path memoryBankPath;
	std::size_t embeddingDimension = 0;
};

class AnomalyPostprocessor final : public IPostprocessor<vision::AnomalyResult> {
public:
	~AnomalyPostprocessor() override;

	[[nodiscard]] static core::Result<std::unique_ptr<AnomalyPostprocessor>> create(
		AnomalyPostprocessorOptions options);

	[[nodiscard]] core::Result<vision::AnomalyResult> process(
		const preprocess::TensorMap& outputs,
		const vision::TransformContext& transformContext,
		const pipeline::PipelinePacket& packet) override;

private:
	AnomalyPostprocessor(
		AnomalyPostprocessorOptions options,
		std::unique_ptr<faiss::IndexFlatL2> memoryBankIndex);

	AnomalyPostprocessorOptions options_;
	std::unique_ptr<faiss::IndexFlatL2> memoryBankIndex_;
};

} // namespace visionRuntime::postprocess