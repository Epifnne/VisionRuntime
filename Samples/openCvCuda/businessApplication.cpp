#include "businessContract.hpp"
#include "cudaThresholdAlgorithm.hpp"
#include "exports/productEntry.hpp"
#include "../common/profileSupport.hpp"
#include "pipeline/openCvPipelineBuilder.hpp"
#include "runtime/runtimeFactory.hpp"
#include <limits>

namespace visionProduct {
using namespace visionRuntime;

core::Result<visionService::ProductApplication> createApplication(
	const visionService::profile::ProductProfile& profile) {
	using Application = visionService::ProductApplication;
	using namespace visionSample::openCvCuda;
	auto manifest = describeApplication(profile);
	if (!manifest) { return core::Result<Application>::failure(manifest.status()); }
	int device = 0;
	double threshold = 127;
	double minimumBrightFraction = 0.5;
	try {
		const auto document = nlohmann::json::parse(profile.documentJson);
		const auto& cuda = document.at("cuda");
		const auto& deviceValue = cuda.at("device");
		if (!deviceValue.is_number_integer() || deviceValue.get<std::int64_t>() < 0 ||
			deviceValue.get<std::int64_t>() > std::numeric_limits<int>::max()) {
			return visionSample::profileError<Application>("cuda.device must be a nonnegative device index");
		}
		device = deviceValue.get<int>();
		threshold = cuda.at("threshold").get<double>();
		minimumBrightFraction = cuda.at("minimumBrightFraction").get<double>();
	} catch (const nlohmann::json::exception& exception) {
		return visionSample::profileError<Application>(std::string("CUDA configuration: ") + exception.what());
	}
	auto algorithm = CudaThresholdAlgorithm::create(device, threshold, minimumBrightFraction);
	if (!algorithm) { return core::Result<Application>::failure(algorithm.status()); }
	auto source = visionSample::createSource(profile.sources[0]);
	if (!source) { return core::Result<Application>::failure(source.status()); }
	Application application;
	application.expectedManifest = manifest.value();
	auto state = businessContract().result.bind(application.exports, CudaSummary{});
	if (!state) { return core::Result<Application>::failure(state.status()); }
	pipeline::OpenCvPipelineBuilder<InspectionResult> builder;
	auto built = builder.setAlgorithm(std::move(algorithm).value()).build();
	if (!built) { return core::Result<Application>::failure(built.status()); }
	executor::FrameExecutionOptions<InspectionResult> execution;
	execution.faultPolicy = profile.pipeline.faultPolicy == "stopSession"
		? executor::FaultPolicy::StopSession : executor::FaultPolicy::ReportAndContinue;
	execution.resultObserver = [state = state.value()](auto, const auto& identity, auto,
		const core::Result<InspectionResult>& result) {
		auto published = state->set(result ? CudaSummary{.sequence = result->sequence,
			.brightFraction = result->brightFraction, .accepted = result->accepted, .valid = true}
			: CudaSummary{.sequence = identity.metadata.sequenceNumber, .error = result.status().toString()});
		if (!published) { logs::report(published.status()); }
	};
	auto runtime = runtime::RuntimeFactory::createRuntime<InspectionResult>(
		std::move(source).value(),
		std::make_unique<pipeline::OpenCvPipeline<InspectionResult>>(std::move(built).value()),
		visionSample::deploymentOptions(profile), std::move(execution));
	if (!runtime) { return core::Result<Application>::failure(runtime.status()); }
	application.session = visionService::session::adoptProductRuntime<InspectionResult>(
		std::move(runtime).value());
	return core::Result<Application>::success(std::move(application));
}
}
