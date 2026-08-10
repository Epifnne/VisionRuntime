#pragma once

#include <memory>

namespace visonRuntime::core {

template<typename T>
class Result;

} // namespace visonRuntime::core

namespace visonRuntime::camera {

class IFrameSource;
struct FrameSourceConfig;

class FrameSourceFactory {
public:
    FrameSourceFactory() = delete;

    [[nodiscard]] static core::Result<std::unique_ptr<IFrameSource>> create(const FrameSourceConfig& config);
};

} // namespace visonRuntime::camera