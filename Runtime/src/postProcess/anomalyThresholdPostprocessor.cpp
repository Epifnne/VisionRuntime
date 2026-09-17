#include "postProcess/anomalyThresholdPostprocessor.hpp"

#include "logs/logger.hpp"

#include <cmath>
#include <utility>

namespace visionRuntime::postprocess {
namespace {

[[nodiscard]] core::Status invalidArgument(const char* message) {
	auto status = core::Status::error(core::StatusCode::InvalidArgument, message);
	logs::report(status);
	return status;
}

} // namespace

core::Result<std::unique_ptr<AnomalyThresholdPostprocessor>>
AnomalyThresholdPostprocessor::create(AnomalyThresholdPostprocessorOptions options) {
	if (options.outputName.empty()) {
		return core::Result<std::unique_ptr<AnomalyThresholdPostprocessor>>::failure(
			invalidArgument("anomaly output name must not be empty"));
	}
	if (!std::isfinite(options.threshold)) {
		return core::Result<std::unique_ptr<AnomalyThresholdPostprocessor>>::failure(
			invalidArgument("anomaly threshold must be finite"));
	}
	return core::Result<std::unique_ptr<AnomalyThresholdPostprocessor>>::success(
		std::unique_ptr<AnomalyThresholdPostprocessor>(
			new AnomalyThresholdPostprocessor(std::move(options))));
}

AnomalyThresholdPostprocessor::AnomalyThresholdPostprocessor(
	AnomalyThresholdPostprocessorOptions options)
	: options_(std::move(options)) {}

core::Result<vision::AnomalyResult> AnomalyThresholdPostprocessor::process(
	const preprocess::TensorMap& outputs,
	const vision::TransformContext& transformContext,
	const pipeline::PipelinePacket& packet) {
	static_cast<void>(transformContext);
	static_cast<void>(packet);

	const auto iterator = outputs.find(options_.outputName);
	if (iterator == outputs.end()) {
		return core::Result<vision::AnomalyResult>::failure(core::Status::error(
			core::StatusCode::InvalidArgument,
			"anomaly output tensor was not found: " + options_.outputName));
	}
	const auto& output = iterator->second;
	if (output.dataType() != core::DataType::Float32 || output.elementCount() != 1) {
		return core::Result<vision::AnomalyResult>::failure(core::Status::error(
			core::StatusCode::Unsupported,
			"anomaly output must be a single Float32 scalar"));
	}

	vision::AnomalyResult result{
		.score = *static_cast<const float*>(output.data()),
		.threshold = options_.threshold,
		.decision = *static_cast<const float*>(output.data()) >= options_.threshold
			? vision::AnomalyDecision::Ng
			: vision::AnomalyDecision::Ok,
	};
	return core::Result<vision::AnomalyResult>::success(result);
}

} // namespace visionRuntime::postprocess