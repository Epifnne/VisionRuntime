#pragma once

#include "camera/frameCallback.hpp"
#include "vision/frameSpec.hpp"

#include <cstddef>
#include <optional>

namespace visionRuntime::camera {

struct FrameSourceInfo {
	vision::FrameSpec outputSpec;
	std::optional<std::size_t> expectedFrameCount;
	bool isFinite = false;
};

/**
 * Asynchronous, single-use frame input with serialized callback delivery.
 *
 * start() rejects an empty callback. After start() succeeds, every later call
 * to start() returns InvalidState, including after natural completion or
 * wait(). Frame errors are delivered through the callback and do not
 * necessarily end a run.
 *
 * requestStop() is thread-safe, non-blocking and idempotent. wait() is
 * idempotent and guarantees that no callback is executing or can begin after
 * it returns. A callback may request stop, but must not call wait().
 */
class IFrameSource {
public:
	virtual ~IFrameSource() = default;

	virtual core::Result<void> start(FrameCallback callback) = 0;
	virtual void requestStop() noexcept = 0;
	virtual void wait() noexcept = 0;
	[[nodiscard]] virtual bool isRunning() const noexcept = 0;
	[[nodiscard]] virtual FrameSourceInfo info() const = 0;
};

} // namespace visionRuntime::camera