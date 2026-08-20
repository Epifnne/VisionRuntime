#pragma once

#include "config/deploymentConfig.hpp"
#include "core/result.hpp"
#include "executor/frameExecutor.hpp"
#include "executor/iPipelineExecutor.hpp"
#include "executor/parallelPipelineExecutor.hpp"
#include "executor/serialPipelineExecutor.hpp"
#include "pipeline/iStagedVisionPipeline.hpp"
#include "pipeline/iVisionPipeline.hpp"
#include "runtime/runtimeSession.hpp"

#include <memory>
#include <utility>

namespace visionRuntime::runtime {

class RuntimeFactory {
public:
	template<typename Preset>
	[[nodiscard]] static auto createFromPreset(typename Preset::Options options) {
		return Preset::create(std::move(options));
	}

	template<typename ResultType>
	[[nodiscard]] static core::Result<std::unique_ptr<RuntimeSession<ResultType>>>
	createRuntime(
		std::unique_ptr<camera::IFrameSource> source,
		std::unique_ptr<pipeline::IVisionPipeline<ResultType>> pipeline,
		const config::DeploymentConfig& config,
		executor::FrameExecutionOptions<ResultType> options = {}) {
		if (!source) {
			return runtimeFailure<ResultType>(core::StatusCode::InvalidArgument,
				"runtime factory requires a frame source");
		}

		auto executor = createExecutor(std::move(pipeline), config);
		if (!executor) {
			return core::Result<std::unique_ptr<RuntimeSession<ResultType>>>::failure(
				executor.status());
		}

		return core::Result<std::unique_ptr<RuntimeSession<ResultType>>>::success(
			std::make_unique<RuntimeSession<ResultType>>(
				std::move(source), std::move(executor).value(),
				std::move(options)));
	}

	template<typename ResultType>
	[[nodiscard]] static core::Result<std::unique_ptr<
		executor::IPipelineExecutor<ResultType>>> createExecutor(
		std::unique_ptr<pipeline::IVisionPipeline<ResultType>> pipeline,
		const config::DeploymentConfig& config) {
		if (!pipeline) {
			return executorFailure<ResultType>(core::StatusCode::InvalidArgument,
				"runtime factory requires a pipeline");
		}
		if (config.executor.queueCapacity == 0 ||
			config.executor.stageQueueCapacity == 0) {
			return executorFailure<ResultType>(core::StatusCode::InvalidArgument,
				"executor queue capacities must be greater than zero");
		}

		executor::ExecutorOptions options;
		options.queueCapacity = config.executor.queueCapacity;
		options.stageQueueCapacity = config.executor.stageQueueCapacity;
		options.queueFullPolicy = config.executor.queueFullPolicy ==
			config::QueueFullPolicy::Block
			? executor::QueueFullPolicy::Block
			: executor::QueueFullPolicy::Drop;

		using Executor = executor::IPipelineExecutor<ResultType>;
		if (config.executor.performancePolicy == config::PerformancePolicy::Serial) {
			return core::Result<std::unique_ptr<Executor>>::success(
				std::make_unique<executor::SerialPipelineExecutor<ResultType>>(
					std::move(pipeline), options));
		}

		auto* stagedPipeline = dynamic_cast<
			pipeline::IStagedVisionPipeline<ResultType>*>(pipeline.get());
		if (stagedPipeline == nullptr) {
			return executorFailure<ResultType>(core::StatusCode::InvalidArgument,
				"pipeline parallel execution requires a staged pipeline");
		}
		static_cast<void>(pipeline.release());
		return core::Result<std::unique_ptr<Executor>>::success(
			std::make_unique<executor::ParallelPipelineExecutor<ResultType>>(
				std::unique_ptr<pipeline::IStagedVisionPipeline<ResultType>>(
					stagedPipeline), options));
	}

private:
	template<typename ResultType>
	[[nodiscard]] static core::Result<std::unique_ptr<RuntimeSession<ResultType>>>
	runtimeFailure(core::StatusCode code, const char* message) {
		return core::Result<std::unique_ptr<RuntimeSession<ResultType>>>::failure(
			core::Status::error(code, message));
	}

	template<typename ResultType>
	[[nodiscard]] static core::Result<std::unique_ptr<
		executor::IPipelineExecutor<ResultType>>> executorFailure(
		core::StatusCode code, const char* message) {
		return core::Result<std::unique_ptr<
			executor::IPipelineExecutor<ResultType>>>::failure(
				core::Status::error(code, message));
	}
};

} // namespace visionRuntime::runtime