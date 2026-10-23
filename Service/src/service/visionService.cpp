#include "service/visionService.hpp"

#include "endpoints/endpointTypes.hpp"
#include "manifest/manifestExporter.hpp"
#include "profile/profileLoader.hpp"
#include "session/sessionAssembler.hpp"
#include "exports/exportContract.hpp"

#include <chrono>
#include <utility>
#include <nlohmann/json.hpp>

namespace visionService {

namespace {

core::Result<void> failure(core::StatusCode code, std::string message) {
	return core::Result<void>::failure(
		core::Status::error(code, std::move(message)));
}

[[nodiscard]] std::string sessionStatePayload(
	session::SessionLifecycle state,
	std::string lastError) {
	nlohmann::json payload{{"state", session::sessionLifecycleName(state)}};
	if (!lastError.empty()) {
		payload["error"] = std::move(lastError);
	}
	return payload.dump();
}

} // namespace

VisionService::VisionService(ProductRuntimeFactory runtimeFactory)
	: dispatcher_(),
	  registry_(dispatcher_),
	  cameraService_(registry_),
	  metrics_(registry_, sessionController_),
	  runtimeFactory_(std::move(runtimeFactory)) {
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

VisionService::VisionService(ProductApplicationFactory applicationFactory)
	: VisionService(ProductRuntimeFactory{}) {
	applicationFactory_ = std::move(applicationFactory);
}

core::Result<void> VisionService::loadProfile(
	const std::filesystem::path& path) {
	if (dispatcher_.isDispatcherThread()) {
		return failure(core::StatusCode::InvalidState,
			"load/recover must run outside endpoint subscription callbacks");
	}
	auto loaded = loadProfileImpl(path);
	if (!loaded) {
		visionRuntime::logs::report(loaded.status());
		shutdown();
	}
	return loaded;
}

core::Result<void> VisionService::loadProfileImpl(const std::filesystem::path& path) {
	shutdown();
	auto profile = profile::ProfileLoader::load(path);
	if (!profile) {
		return failure(profile.status().code(),
			std::string(profile.status().message()));
	}
	if (!runtimeFactory_ && !applicationFactory_ && !profile->runtimeProduct.empty()) {
		auto status = core::Status::error(core::StatusCode::Unsupported,
			"profile requires product Shell runtime: " + profile->runtimeProduct);
		visionRuntime::logs::report(status);
		return core::Result<void>::failure(std::move(status));
	}
	profile_ = std::move(profile).value();
	profilePath_ = std::filesystem::absolute(path);
	metrics_.reset();
	streamBindings_.reset();
	timedPipeline_ = nullptr;

	auto registered = registerCoreEndpoints();
	if (!registered) {
		return registered;
	}

	auto observeOutcome = [this, observe = metrics_.resultObserver()](auto outcome, const auto& identity,
			auto taskId, const auto& result) {
			observe(outcome, identity, taskId, result);
			if (resultObserver) {
				resultObserver(outcome, identity, taskId, result);
			}
		};
	auto sourceFaultObserver = [this](const auto& fault) { metrics_.observeSourceFault(fault); };
	auto assembled = [&]() -> core::Result<session::AssembledSession> {
		if (!runtimeFactory_ && !applicationFactory_) {
			return session::SessionAssembler::assemble(profile_, registry_,
				metrics_.timingObserver(), metrics_.batchObserver(),
				observeOutcome, sourceFaultObserver);
		}
		auto runtime = [&]() -> core::Result<std::unique_ptr<session::IInspectionSession>> {
			try {
				if (applicationFactory_) {
					auto application = applicationFactory_(profile_);
					if (!application) {
						return core::Result<std::unique_ptr<session::IInspectionSession>>::failure(
							application.status());
					}
					productExports_ = std::make_unique<exports::BusinessExports>(
						std::move(application->exports));
					expectedProductManifest_ = std::move(application->expectedManifest);
					auto exported = productExports_->registerWith(registry_);
					if (!exported) {
						return core::Result<std::unique_ptr<session::IInspectionSession>>::failure(
							exported.status());
					}
					return core::Result<std::unique_ptr<session::IInspectionSession>>::success(
						std::move(application->session));
				}
				return runtimeFactory_(profile_, registry_);
			} catch (const std::exception& exception) {
				auto status = core::Status::error(core::StatusCode::Internal,
					std::string("product runtime factory threw: ") + exception.what());
				visionRuntime::logs::report(status);
				return core::Result<std::unique_ptr<session::IInspectionSession>>::failure(
					std::move(status));
			}
		}();
		if (!runtime) {
			visionRuntime::logs::report(runtime.status());
			return core::Result<session::AssembledSession>::failure(runtime.status());
		}
		session::SessionObservers observers;
		observers.sourceFault = sourceFaultObserver;
		if (applicationFactory_) {
			observers.outcome = [this](auto kind, const auto& identity, auto taskId,
				const auto& status) {
				metrics_.observeOutcome(kind, identity, taskId, status);
			};
		} else {
			observers.result = observeOutcome;
		}
		return session::SessionAssembler::bindRuntime(std::move(runtime).value(),
			profile_, registry_, std::move(observers));
	}();
	if (!assembled) {
		return failure(assembled.status().code(),
			std::string(assembled.status().message()));
	}
	streamBindings_ = assembled->streamBindings;

	auto configured = sessionController_.configureSession(
		std::move(assembled->session), assembled->sourceCount);
	if (!configured) {
		return configured;
	}
	timedPipeline_ = assembled->timedPipeline;

	for (std::size_t index = 0; index < profile_.sources.size(); ++index) {
		auto cameraEndpoints = cameraService_.registerCameraEndpoints(
			index, profile_.sources[index].id, assembled->cameraDevices[index],
			assembled->directTriggers.empty() || assembled->directTriggers[index]);
		if (!cameraEndpoints) {
			return cameraEndpoints;
		}
	}

	auto exposed = applyEndpointExposure();
	if (!exposed) {
		return exposed;
	}
	if (!expectedProductManifest_.empty()) {
		auto compatible = exports::validateExportContract(
			expectedProductManifest_, exportManifest());
		if (!compatible) {
			return compatible;
		}
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
	auto document = nlohmann::json::parse(manifest::exportManifest(registry_));
	if (!expectedProductManifest_.empty()) {
		document["runtimeProduct"] = profile_.runtimeProduct;
	}
	return document.dump(2);
}

void VisionService::shutdown() noexcept {
	{
		std::lock_guard lock(metricsThreadMutex_);
		metricsThread_ = {};
	}
	sessionController_.requestStop(
		visionRuntime::executor::StopMode::Immediate);
	sessionController_.waitForIdle();
	if (productExports_) {
		productExports_->disconnect();
	}
	dispatcher_.drain();
	registry_.clear();
	sessionController_.releaseSession();
	productExports_.reset();
	expectedProductManifest_.clear();
	profileLoaded_ = false;
	timedPipeline_ = nullptr;
}

void VisionService::metricsPublishSummary() noexcept {
	metrics_.publishSummary();
	metrics_.publishSessionState();
}

core::Result<void> VisionService::registerCoreEndpoints() {
	auto registered = registry_.registerCommand({
		.name = "session.start",
		.description = "start the detection session",
		.accessLevel = endpoints::AccessLevel::Operator,
		.invoke = [this] {
		auto started = sessionController_.start();
			metrics_.publishSessionState();
			if (started &&
				sessionController_.state() == session::SessionLifecycle::Running) {
				// Live publish starts only after start() succeeded so no sample
				// is missed between the last tick and the final publish. The
				// stop request is interrupted before the control thread finishes,
				// so the final summary/performance are never overwritten by a
				// racing live tick.
				std::lock_guard lock(metricsThreadMutex_);
				if (sessionController_.state() != session::SessionLifecycle::Running) {
					return started;
				}
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
		.name = "session.recover",
		.description = "reload the profile and recreate camera/model resources; does not start detection",
		.accessLevel = endpoints::AccessLevel::Engineer,
		.invoke = [this] {
			if (sessionController_.state() != session::SessionLifecycle::Idle) {
				return failure(core::StatusCode::InvalidState,
					"stop the session and wait for idle before recovering");
			}
			const auto path = profilePath_;
			return loadProfile(path);
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

	registered = registry_.registerState({
		.name = "inspection.result",
		.description = "latest correlated inspection outcome; subscribe for immutable outcome snapshots",
		.accessLevel = endpoints::AccessLevel::Operator,
		.current = [this] {
			return endpoints::StateSnapshot{0, metrics_.resultPayload()};
		},
	});
	if (!registered) {
		return registered;
	}
	return registry_.registerState({
		.name = "source.fault",
		.description = "latest acquisition fault or frame-sequence gap",
		.accessLevel = endpoints::AccessLevel::Operator,
		.current = [this] {
			return endpoints::StateSnapshot{0, metrics_.sourceFaultPayload()};
		},
	});
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
