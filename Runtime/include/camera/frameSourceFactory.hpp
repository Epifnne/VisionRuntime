#pragma once

#include <memory>

namespace visionRuntime::core {

template<typename T>
class Result;

} // namespace visionRuntime::core

namespace visionRuntime::camera {

class IFrameSource;
struct FrameSourceConfig;

class FrameSourceFactory {
public:
    FrameSourceFactory() = delete;

    [[nodiscard]] static core::Result<std::unique_ptr<IFrameSource>> create(const FrameSourceConfig& config);
};

} // namespace visionRuntime::camera