#pragma once

#include "core/result.hpp"
#include "vision/frame.hpp"

#include <functional>

namespace visionRuntime::camera {

using FrameCallback = std::function<void(core::Result<vision::Frame>)>;

class IFrameSource {
public:
	virtual ~IFrameSource() = default;

	virtual core::Result<void> start(FrameCallback callback) = 0;
	virtual void requestStop() noexcept = 0;
	virtual void wait() noexcept = 0;
	[[nodiscard]] virtual bool isRunning() const noexcept = 0;
};

} // namespace visionRuntime::camera