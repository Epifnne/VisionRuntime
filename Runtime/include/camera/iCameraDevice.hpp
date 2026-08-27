/**
 * @file iCameraDevice.hpp
 * @author epifnne
 * @date 2026-08-25
 * @brief Declares vendor-neutral industrial camera controls.
 */

#pragma once

#include "camera/cameraTypes.hpp"
#include "camera/frameCallback.hpp"

namespace visionRuntime::camera {

/**
 * Vendor-neutral, single-use camera acquisition device.
 *
 * The device owns transport and SDK resources, but does not decide when a
 * software trigger should be issued. startAcquisition() configures the camera
 * for the requested mode and optional device frame rate, then starts serialized
 * frame delivery. In
 * SoftwareTrigger mode no frame is requested until softwareTrigger() is called.
 *
 * After startAcquisition() succeeds, every later startAcquisition() call returns
 * InvalidState. requestStop() is thread-safe, non-blocking and idempotent.
 * wait() is idempotent and guarantees that no callback can begin after it
 * returns. A frame callback may request stop, but must not call wait().
 */
class ICameraDevice {
public:
	virtual ~ICameraDevice() = default;

	virtual core::Result<void> startAcquisition(
		CameraAcquisitionOptions options, FrameCallback callback) = 0;
	virtual void requestStop() noexcept = 0;
	virtual void wait() noexcept = 0;
	[[nodiscard]] virtual bool isAcquiring() const noexcept = 0;
	virtual core::Result<void> softwareTrigger() = 0;
	[[nodiscard]] virtual const CameraDeviceInfo& deviceInfo() const noexcept = 0;
	[[nodiscard]] virtual const CameraCapabilities& capabilities() const noexcept = 0;
	[[nodiscard]] virtual vision::FrameSpec outputSpec() const = 0;
};

} // namespace visionRuntime::camera