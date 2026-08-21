#include "postProcess/anomalyThresholdPostprocessor.hpp"

#include <cmath>
#include <memory>
#include <utility>

namespace visionRuntime::postprocess {
namespace {

[[nodiscard]] core::Status invalidArgument(const char* message) {
	return core::Status::error(core::StatusCode::InvalidArgument, message);
}

} // namespace

core::Result<std::unique_ptr<AnomalyThresholdPostprocessor>>
AnomalyThresholdPostprocessor::create(
	std::unique_ptr<IPostprocessor<vision::AnomalyResult>> scorePostprocessor,
	AnomalyThresholdPostprocessorOptions options) {
	if (!scorePostprocessor) {
		return core::Result<std::unique_ptr<AnomalyThresholdPostprocessor>>::failure(
			invalidArgument("anomaly score postprocessor must not be null"));
	}
	if (!std::isfinite(options.threshold)) {
		return core::Result<std::unique_ptr<AnomalyThresholdPostprocessor>>::failure(
			invalidArgument("anomaly threshold must be finite"));
	}
	return core::Result<std::unique_ptr<AnomalyThresholdPostprocessor>>::success(
		std::unique_ptr<AnomalyThresholdPostprocessor>(
			new AnomalyThresholdPostprocessor(
				std::move(scorePostprocessor), options)));
}

AnomalyThresholdPostprocessor::AnomalyThresholdPostprocessor(
	std::unique_ptr<IPostprocessor<vision::AnomalyResult>> scorePostprocessor,
	AnomalyThresholdPostprocessorOptions options)
	: scorePostprocessor_(std::move(scorePostprocessor)), options_(options) {}

core::Result<vision::AnomalyResult> AnomalyThresholdPostprocessor::process(
	const preprocess::TensorMap& outputs,
	const vision::TransformContext& transformContext,
	const pipeline::PipelinePacket& packet) {
	auto result = scorePostprocessor_->process(outputs, transformContext, packet);
	if (!result) {
		return result;
	}
	result->threshold = options_.threshold;
	result->decision = result->score >= options_.threshold
		? vision::AnomalyDecision::Ng
		: vision::AnomalyDecision::Ok;
	return result;
}

} // namespace visionRuntime::postprocess