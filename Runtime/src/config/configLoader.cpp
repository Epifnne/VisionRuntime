#include "config/configLoader.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <fstream>
#include <string>

namespace visionRuntime::config {
namespace {

[[nodiscard]] core::Result<DeploymentConfig> invalidConfig(
	core::StatusCode code, const std::string& message) {
	return core::Result<DeploymentConfig>::failure(
		core::Status::error(code, message));
}

} // namespace

core::Result<DeploymentConfig> ConfigLoader::loadDeployment(
	const std::filesystem::path& path) {
	std::ifstream stream(path);
	if (!stream) {
		return invalidConfig(core::StatusCode::Unavailable,
			"deployment config could not be opened: " + path.string());
	}

	try {
		const auto document = nlohmann::json::parse(stream);
		const auto& executor = document.at("executor");
		DeploymentConfig config;

		const auto performancePolicy = executor.at("performancePolicy").get<std::string>();
		if (performancePolicy == "serial") {
			config.executor.performancePolicy = PerformancePolicy::Serial;
		} else if (performancePolicy == "pipelineParallel") {
			config.executor.performancePolicy = PerformancePolicy::PipelineParallel;
		} else {
			return invalidConfig(core::StatusCode::InvalidArgument,
				"executor.performancePolicy must be serial or pipelineParallel");
		}

		const auto queueFullPolicy = executor.at("queueFullPolicy").get<std::string>();
		if (queueFullPolicy == "drop") {
			config.executor.queueFullPolicy = QueueFullPolicy::Drop;
		} else if (queueFullPolicy == "block") {
			config.executor.queueFullPolicy = QueueFullPolicy::Block;
		} else {
			return invalidConfig(core::StatusCode::InvalidArgument,
				"executor.queueFullPolicy must be drop or block");
		}

		config.executor.queueCapacity = executor.at("queueCapacity").get<std::size_t>();
		config.executor.stageQueueCapacity =
			executor.value("stageQueueCapacity", std::size_t{1});
		if (config.executor.queueCapacity == 0 ||
			config.executor.stageQueueCapacity == 0) {
			return invalidConfig(core::StatusCode::InvalidArgument,
				"executor queue capacities must be greater than zero");
		}
		return core::Result<DeploymentConfig>::success(config);
	} catch (const std::exception& exception) {
		return invalidConfig(core::StatusCode::InvalidArgument,
			std::string("invalid deployment config: ") + exception.what());
	}
}

} // namespace visionRuntime::config