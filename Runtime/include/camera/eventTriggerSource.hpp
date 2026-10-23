#pragma once

#include "camera/iCameraDevice.hpp"

#include <chrono>
#include <functional>
#include <memory>

namespace visionRuntime::camera {

struct CaptureRequest {
	vision::CaptureIdentity identity;
	std::chrono::steady_clock::time_point deadline;
};

struct EventTriggerOptions {
	std::size_t queueCapacity = 16;
	std::chrono::milliseconds responseTimeout{1000};
};

using CaptureCallback = std::function<void(
	const CaptureRequest&, core::Result<vision::Frame>)>;

// Single-use, externally driven acquisition. One outstanding device trigger at a
// time; a response timeout permanently faults this source until it is recreated.
class EventTriggerSource {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<EventTriggerSource>> create(
		std::unique_ptr<ICameraDevice> device, EventTriggerOptions options = {});
	~EventTriggerSource();

	EventTriggerSource(const EventTriggerSource&) = delete;
	EventTriggerSource& operator=(const EventTriggerSource&) = delete;

	[[nodiscard]] core::Result<void> start(CaptureCallback callback,
		std::function<void(const core::Status&)> faultObserver = {});
	// Rejected requests never invoke the callback. Accepted requests invoke it
	// exactly once, including cancellation. Callbacks must not call wait().
	[[nodiscard]] core::Result<void> request(CaptureRequest request);
	void requestStop() noexcept;
	void wait() noexcept;
	[[nodiscard]] ICameraDevice* cameraDevice() const noexcept;
	[[nodiscard]] bool faulted() const;

private:
	class Impl;
	explicit EventTriggerSource(std::unique_ptr<Impl> impl);
	std::unique_ptr<Impl> impl_;
};

} // namespace visionRuntime::camera
