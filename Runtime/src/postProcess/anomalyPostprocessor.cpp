#include "postProcess/anomalyPostprocessor.hpp"

#include "core/dataType.hpp"

#include <cmath>
#include <memory>
#include <utility>

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
	return core::Result<std::unique_ptr<AnomalyPostprocessor>>::success(
		std::unique_ptr<AnomalyPostprocessor>(
			new AnomalyPostprocessor(std::move(options))));
}

AnomalyPostprocessor::AnomalyPostprocessor(AnomalyPostprocessorOptions options)
	: options_(std::move(options)) {}

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

	const auto score = *static_cast<const float*>(tensor.data());
	if (!std::isfinite(score)) {
		return core::Result<vision::AnomalyResult>::failure(
			invalidArgument("anomaly score must be finite"));
	}

	return core::Result<vision::AnomalyResult>::success(
		vision::AnomalyResult{.score = score});
}

} // namespace visionRuntime::postprocess