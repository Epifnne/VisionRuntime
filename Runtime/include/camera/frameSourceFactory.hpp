#pragma once

#include "camera/frameSourceConfig.hpp"
#include "camera/iFrameSource.hpp"
#include "core/result.hpp"

#include <memory>

namespace visionRuntime::camera {

class FrameSourceFactory {
public:
    FrameSourceFactory() = delete;

    [[nodiscard]] static core::Result<std::unique_ptr<IFrameSource>> create(
        const FrameSourceConfig& config);
};

} // namespace visionRuntime::camera