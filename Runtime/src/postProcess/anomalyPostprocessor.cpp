#include "postProcess/anomalyPostprocessor.hpp"

#include "core/dataType.hpp"

#include <faiss/IndexFlat.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

namespace visionRuntime::postprocess {
namespace {

[[nodiscard]] core::Status invalidArgument(std::string message) {
	return core::Status::error(core::StatusCode::InvalidArgument, std::move(message));
}

} // namespace

core::Result<std::unique_ptr<AnomalyPostprocessor>> AnomalyPostprocessor::create(
	AnomalyPostprocessorOptions options) {
	if (options.outputName.empty()) {
		return core::Result<std::unique_ptr<AnomalyPostprocessor>>::failure(
			invalidArgument("anomaly output name must not be empty"));
	}
	std::unique_ptr<faiss::IndexFlatL2> memoryBankIndex;
	if (!options.memoryBankPath.empty()) {
		if (options.embeddingDimension == 0) {
			return core::Result<std::unique_ptr<AnomalyPostprocessor>>::failure(
				invalidArgument("anomaly embedding dimension must be greater than zero"));
		}
		std::error_code error;
		const auto byteCount = std::filesystem::file_size(options.memoryBankPath, error);
		const auto vectorByteCount = options.embeddingDimension * sizeof(float);
		if (error || byteCount == 0 || byteCount % vectorByteCount != 0) {
			return core::Result<std::unique_ptr<AnomalyPostprocessor>>::failure(
				invalidArgument("anomaly memory bank must contain complete Float32 vectors: " +
					options.memoryBankPath.string()));
		}

		std::vector<float> values(byteCount / sizeof(float));
		std::ifstream stream(options.memoryBankPath, std::ios::binary);
		if (!stream.read(reinterpret_cast<char*>(values.data()),
				static_cast<std::streamsize>(byteCount))) {
			return core::Result<std::unique_ptr<AnomalyPostprocessor>>::failure(
				invalidArgument("failed to read anomaly memory bank: " +
					options.memoryBankPath.string()));
		}
		try {
			memoryBankIndex = std::make_unique<faiss::IndexFlatL2>(
				static_cast<faiss::idx_t>(options.embeddingDimension));
			memoryBankIndex->add(
				static_cast<faiss::idx_t>(values.size() / options.embeddingDimension),
				values.data());
		} catch (const std::exception& exception) {
			return core::Result<std::unique_ptr<AnomalyPostprocessor>>::failure(
				invalidArgument("failed to build anomaly memory bank index: " +
					std::string(exception.what())));
		}
	}
	return core::Result<std::unique_ptr<AnomalyPostprocessor>>::success(
		std::unique_ptr<AnomalyPostprocessor>(
			new AnomalyPostprocessor(std::move(options), std::move(memoryBankIndex))));
}

AnomalyPostprocessor::AnomalyPostprocessor(
	AnomalyPostprocessorOptions options,
	std::unique_ptr<faiss::IndexFlatL2> memoryBankIndex)
	: options_(std::move(options)), memoryBankIndex_(std::move(memoryBankIndex)) {}

AnomalyPostprocessor::~AnomalyPostprocessor() = default;

core::Result<vision::AnomalyResult> AnomalyPostprocessor::process(
	const preprocess::TensorMap& outputs,
	const vision::TransformContext&,
	const pipeline::PipelinePacket&) {
	const auto iterator = outputs.find(options_.outputName);
	if (iterator == outputs.end()) {
		return core::Result<vision::AnomalyResult>::failure(
			invalidArgument("anomaly output tensor was not found: " + options_.outputName));
	}
	const auto& tensor = iterator->second;
	if (tensor.dataType() != core::DataType::Float32 || tensor.elementCount() < 1 ||
		tensor.data() == nullptr) {
		return core::Result<vision::AnomalyResult>::failure(invalidArgument(
			"anomaly output must be a host-accessible Float32 tensor with at least one value"));
	}

	float score = 0.0F;
	if (memoryBankIndex_) {
		if (tensor.elementCount() % options_.embeddingDimension != 0) {
			return core::Result<vision::AnomalyResult>::failure(invalidArgument(
				"anomaly embedding tensor size is not divisible by its dimension"));
		}
		const auto patchCount = tensor.elementCount() / options_.embeddingDimension;
		std::vector<float> distances(patchCount);
		std::vector<faiss::idx_t> labels(patchCount);
		try {
			memoryBankIndex_->search(
				static_cast<faiss::idx_t>(patchCount),
				static_cast<const float*>(tensor.data()), 1, distances.data(), labels.data());
		} catch (const std::exception& exception) {
			return core::Result<vision::AnomalyResult>::failure(
				invalidArgument("anomaly memory bank search failed: " +
					std::string(exception.what())));
		}
		score = *std::max_element(distances.begin(), distances.end());
	} else {
		score = *static_cast<const float*>(tensor.data());
	}
	if (!std::isfinite(score)) {
		return core::Result<vision::AnomalyResult>::failure(
			invalidArgument("anomaly score must be finite"));
	}

	return core::Result<vision::AnomalyResult>::success(
		vision::AnomalyResult{.score = score});
}

} // namespace visionRuntime::postprocess