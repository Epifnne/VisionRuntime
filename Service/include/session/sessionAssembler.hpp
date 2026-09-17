/**
 * @file sessionAssembler.hpp
 * @brief Assembles sources, pipeline, batch executor and session from a ProductProfile.
 */

#pragma once

#include "benchmark/time.hpp"
#include "benchmark/timedPipeline.hpp"
#include "camera/iCameraDevice.hpp"
#include "core/result.hpp"
#include "endpoints/endpointRegistry.hpp"
#include "profile/productProfile.hpp"
#include "runtime/multiCameraSession.hpp"
#include "session/sessionController.hpp"
#include "vision/anomalyResult.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace visionService::session {

namespace camera = visionRuntime::camera;
namespace core = visionRuntime::core;
namespace runtime = visionRuntime::runtime;

struct AssembledSession {
	std::unique_ptr<runtime::MultiCameraSession<SessionResult>> session;
	/// One entry per profile source, in source order; null for directory sources.
	std::vector<camera::ICameraDevice*> cameraDevices;
	/// Owns the source-id and stream-name pairs used to fan frames out to the
	/// registered Stream endpoints before the session consumes them.
	std::shared_ptr<std::vector<std::pair<std::uint32_t, std::string>>>
		streamBindings;
	/// Non-owning; owned by the executor inside `session`. Valid for the
	/// session's lifetime; used for live performance snapshots.
	visionRuntime::benchmark::TimedPipeline<SessionResult>* timedPipeline =
		nullptr;
	std::size_t sourceCount = 0;
};

class SessionAssembler {
public:
	SessionAssembler() = delete;

	[[nodiscard]] static core::Result<AssembledSession> assemble(
		const profile::ProductProfile& profile,
		endpoints::EndpointRegistry& registry,
		visionRuntime::benchmark::PipelineTimingObserver<SessionResult>
			timingObserver = {},
		visionRuntime::benchmark::BatchPerformanceObserver batchObserver = {});
};

} // namespace visionService::session
