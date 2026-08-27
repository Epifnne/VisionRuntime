#pragma once

#include "core/result.hpp"
#include "vision/frame.hpp"

#include <functional>

namespace visionRuntime::camera {

using FrameCallback = std::function<void(core::Result<vision::Frame>)>;

} // namespace visionRuntime::camera
