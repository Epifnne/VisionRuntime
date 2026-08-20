#pragma once

#include "camera/iFrameSource.hpp"
#include "executor/frameExecutor.hpp"
#include "executor/iPipelineExecutor.hpp"

#include <memory>
#include <utility>

namespace visionRuntime::runtime {

template<typename ResultType>
class RuntimeSession {
public:
	RuntimeSession(
		std::unique_ptr<camera::IFrameSource> source,
		std::unique_ptr<executor::IPipelineExecutor<ResultType>> executor,
		executor::FrameExecutionOptions<ResultType> options = {})
		: frameExecutor_(
			std::move(source), std::move(executor), std::move(options)) {}

	RuntimeSession(const RuntimeSession&) = delete;
	RuntimeSession& operator=(const RuntimeSession&) = delete;
	RuntimeSession(RuntimeSession&&) = delete;
	RuntimeSession& operator=(RuntimeSession&&) = delete;

	[[nodiscard]] core::Result<void> start() {
		return frameExecutor_.start();
	}

	[[nodiscard]] executor::FrameExecutionSummary wait() {
		return frameExecutor_.wait();
	}

	void stop() noexcept {
		frameExecutor_.stop();
	}

private:
	executor::FrameExecutor<ResultType> frameExecutor_;
};

} // namespace visionRuntime::runtime