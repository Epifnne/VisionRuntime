#include "service/visionService.hpp"

#include "endpoints/endpointTypes.hpp"
#include "manifest/manifestExporter.hpp"
#include "profile/profileLoader.hpp"
#include "session/sessionAssembler.hpp"

#include <chrono>
#include <utility>

namespace visionService {

namespace {

core::Result<void> failure(core::StatusCode code, std::string message) {
	return core::Result<void>::failure(
		core::Status::error(code, std::move(message)));
}

[[nodiscard]] std::string sessionStatePayload(
	session::SessionLifecycle state,
	std::string lastError) {
	std::string payload = R"({"state":")";
	payload += session::sessionLifecycleName(state);
	payload += '"';
	if (!lastError.empty()) {
		payload += R"(,"error":")";
		payload += std::move(lastError);
		payload += '"';
	}
	payload += '}';
	return payload;
}

} // namespace

VisionService::VisionService()
	: dispatcher_(),
	  registry_(dispatcher_),
	  cameraService_(registry_),
	  metrics_(registry_, sessionController_) {
	sessionController_.completionCallback =
		[this](const session::SessionCounters&) {
			{
				std::lock_guard lock(metricsThreadMutex_);
				metricsThread_ = {};
			}
			metricsPublishSummary();
		};
}

VisionService::~VisionService() {
	shutdown();
}

core::Result<void> VisionService::loadProfile(
	const std::filesystem::path& path) {
	shutdown();
	auto profile = profile::ProfileLoader::load(path);
	if (!profile) {
		return failure(profile.status().code(),
			std::string(profile.status().message()));
	}
	profile_ = std::move(profile).value();
	streamBindings_.reset();
	timedPipeline_ = nullptr;

	auto registered = registerCoreEndpoints();
	if (!registered) {
		return registered;
	}

	auto assembled = session::SessionAssembler::assemble(
		profile_, registry_, metrics_.timingObserver(), metrics_.batchObserver());
	if (!assembled) {
		return failure(assembled.status().code(),
			std::string(assembled.status().message()));
	}
	streamBindings_ = assembled->streamBindings;

	auto configured = sessionController_.configure(
		std::move(assembled->session), assembled->sourceCount);
	if (!configured) {
		return configured;
	}
	timedPipeline_ = assembled->timedPipeline;

	for (std::size_t index = 0; index < profile_.sources.size(); ++index) {
		auto cameraEndpoints = cameraService_.registerCameraEndpoints(
			index, profile_.sources[index].id, assembled->cameraDevices[index]);
		if (!cameraEndpoints) {
			return cameraEndpoints;
		}
	}

	auto exposed = applyEndpointExposure();
	if (!exposed) {
		return exposed;
	}

	profileLoaded_ = true;
	metrics_.publishSessionState();
	metrics_.publishSummary();
	return core::Result<void>::success();
}

endpoints::EndpointRegistry& VisionService::endpoints() noexcept {
	return registry_;
}

session::SessionController& VisionService::sessionController() noexcept {
	return sessionController_;
}

serviceCamera::CameraService& VisionService::cameraService() noexcept {
	return cameraService_;
}

const profile::ProductProfile& VisionService::profile() const noexcept {
	return profile_;
}

std::string VisionService::exportManifest() const {
	return manifest::exportManifest(registry_);
}

void VisionService::shutdown() noexcept {
	{
		std::lock_guard lock(metricsThreadMutex_);
		metricsThread_ = {};
	}
	sessionController_.requestStop(
		visionRuntime::executor::StopMode::Immediate);
	registry_.clear();
	profileLoaded_ = false;
	timedPipeline_ = nullptr;
}

void VisionService::metricsPublishSummary() noexcept {
	metrics_.publishSessionState();
	metrics_.publishSummary();
}

core::Result<void> VisionService::registerCoreEndpoints() {
	auto registered = registry_.registerCommand({
		.name = "session.start",
		.description = "start the detection session",
		.accessLevel = endpoints::AccessLevel::Operator,
		.invoke = [this] {
		auto started = sessionController_.start();
			metrics_.publishSessionState();
			if (started) {
				// Live publish starts only after start() succeeded so no sample
				// is missed between the last tick and the final publish. The
				// stop request is interrupted before the control thread finishes,
				// so the final summary/performance are never overwritten by a
				// racing live tick.
				std::lock_guard lock(metricsThreadMutex_);
				metricsThread_ = std::jthread([this](std::stop_token stop) {
					while (!stop.stop_requested()) {
						metrics_.publishLive(timedPipeline_);
						for (int slice = 0; slice < 50 && !stop.stop_requested();
							 ++slice) {
							std::this_thread::sleep_for(
								std::chrono::milliseconds(10));
						}
					}
				});
			}
			return started;
		},
	});
	if (!registered) {
		return registered;
	}

	registered = registry_.registerCommand({
		.name = "session.stop",
		.description = "request a graceful stop of the running session",
		.accessLevel = endpoints::AccessLevel::Operator,
		.invoke = [this] {
			// Interrupt live publishing before the run drains; the final
			// summary/performance (published when the control thread finishes)
			// must not be overwritten by a racing live tick.
			{
				std::lock_guard lock(metricsThreadMutex_);
				metricsThread_ = {};
			}
			sessionController_.requestStop();
			metrics_.publishSessionState();
			return core::Result<void>::success();
		},
	});
	if (!registered) {
		return registered;
	}

	registered = registry_.registerState({
		.name = "session.state",
		.description = "session lifecycle state",
		.accessLevel = endpoints::AccessLevel::Operator,
		.current = [this] {
			return endpoints::StateSnapshot{
				.version = 0,
				.payload = sessionStatePayload(
					sessionController_.state(), sessionController_.lastError()),
			};
		},
	});
	if (!registered) {
		return registered;
	}

	registered = registry_.registerState({
		.name = "session.summary",
		.description = "session counters and per-source statistics",
		.accessLevel = endpoints::AccessLevel::Operator,
		.current = [this] {
			return endpoints::StateSnapshot{
				.version = 0,
				.payload = metrics_.summaryPayloadForController(),
			};
		},
	});
	if (!registered) {
		return registered;
	}

	registered = registry_.registerState({
		.name = "metrics.performance",
		.description = "last batch performance counters",
		.accessLevel = endpoints::AccessLevel::Operator,
		.current = [this] {
			return endpoints::StateSnapshot{
				.version = 0,
				.payload = metrics_.performancePayload(),
			};
		},
	});
	if (!registered) {
		return registered;
	}

	return core::Result<void>::success();
}

core::Result<void> VisionService::applyEndpointExposure() {
	for (const auto& exposure : profile_.endpoints) {
		if (!registry_.contains(exposure.name)) {
			return failure(core::StatusCode::InvalidArgument,
				"profile exposes an endpoint that is not registered: " +
					exposure.name);
		}
		endpoints::AccessLevel level = endpoints::AccessLevel::Operator;
		if (exposure.access == "engineer") {
			level = endpoints::AccessLevel::Engineer;
		} else if (exposure.access == "administrator") {
			level = endpoints::AccessLevel::Administrator;
		} else if (exposure.access != "operator") {
			return failure(core::StatusCode::InvalidArgument,
				"unknown endpoint access level: " + exposure.access);
		}
		auto applied = registry_.setAccessLevel(exposure.name, level);
		if (!applied) {
			return applied;
		}
	}
	return core::Result<void>::success();
}

} // namespace visionService
