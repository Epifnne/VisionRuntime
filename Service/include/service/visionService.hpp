/**
 * @file visionService.hpp
 * @brief Application service facade: profile loading, session lifecycle, camera
 * service, metrics aggregation and the endpoint registry contract.
 */

#pragma once

#include "camera/cameraService.hpp"
#include "core/result.hpp"
#include "endpoints/endpointDispatcher.hpp"
#include "endpoints/endpointRegistry.hpp"
#include "metrics/metricsAggregator.hpp"
#include "profile/productProfile.hpp"
#include "session/sessionController.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace visionService {

namespace serviceCamera = visionService::camera;

class VisionService {
public:
	VisionService();
	~VisionService();

	VisionService(const VisionService&) = delete;
	VisionService& operator=(const VisionService&) = delete;
	VisionService(VisionService&&) = delete;
	VisionService& operator=(VisionService&&) = delete;

	[[nodiscard]] core::Result<void> loadProfile(
		const std::filesystem::path& path);

	[[nodiscard]] endpoints::EndpointRegistry& endpoints() noexcept;
	[[nodiscard]] session::SessionController& sessionController() noexcept;
	[[nodiscard]] serviceCamera::CameraService& cameraService() noexcept;
	[[nodiscard]] const profile::ProductProfile& profile() const noexcept;

	/// Registry manifest JSON; consumed by visionDesigner.
	[[nodiscard]] std::string exportManifest() const;

	/// Stops the session (if running) and clears all registered endpoints.
	void shutdown() noexcept;

	/// Publishes the session summary and lifecycle state snapshots.
	void metricsPublishSummary() noexcept;

private:
	[[nodiscard]] core::Result<void> registerCoreEndpoints();
	[[nodiscard]] core::Result<void> applyEndpointExposure();

	endpoints::EndpointDispatcher dispatcher_;
	endpoints::EndpointRegistry registry_;
	session::SessionController sessionController_;
	serviceCamera::CameraService cameraService_;
	metrics::MetricsAggregator metrics_;
	profile::ProductProfile profile_;
	bool profileLoaded_ = false;
	std::shared_ptr<std::vector<std::pair<std::uint32_t, std::string>>>
		streamBindings_;
	/// Non-owning; owned by the executor of the current session. Null when no
	/// profile is loaded.
	visionRuntime::benchmark::TimedPipeline<session::SessionResult>*
		timedPipeline_ = nullptr;
	/// Periodically publishes live summary/performance while a run is active.
	std::jthread metricsThread_;
	/// Guards metricsThread_ (assigned by command invocations, the session
	/// completion callback and shutdown from different threads).
	std::mutex metricsThreadMutex_;
};

} // namespace visionService
