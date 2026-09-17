#include "config/configLoader.hpp"

#include "logs/logger.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <fstream>
#include <string>
#include <utility>

namespace visionRuntime::config {
namespace {

[[nodiscard]] core::Result<DeploymentConfig> invalidConfig(
	core::StatusCode code, const std::string& message) {
	auto status = core::Status::error(code, message);
	logs::report(status);
	return core::Result<DeploymentConfig>::failure(std::move(status));
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
		const auto& schemaVersion = document.at("schemaVersion");
		if (schemaVersion.at("major").get<std::uint32_t>() != 1) {
			return invalidConfig(core::StatusCode::Unsupported,
				"deployment schema major version is unsupported");
		}

		const auto& backend = document.at("backend");
		const auto& executor = document.at("executor");
		DeploymentConfig config;
		const auto pluginDirectory =
			backend.at("pluginDirectory").get<std::string>();
		config.backend.id = backend.at("id").get<std::string>();
		config.backend.device = backend.at("device").get<std::string>();
		if (pluginDirectory.empty() || config.backend.id.empty() ||
			config.backend.device.empty()) {
			return invalidConfig(core::StatusCode::InvalidArgument,
				"backend pluginDirectory, id, and device must not be empty");
		}
		config.backend.pluginDirectory = std::filesystem::path(std::u8string(
			reinterpret_cast<const char8_t*>(pluginDirectory.data()), pluginDirectory.size()));
		if (config.backend.pluginDirectory.is_relative()) {
			config.backend.pluginDirectory =
				std::filesystem::absolute(path).parent_path() /
				config.backend.pluginDirectory;
		}
		config.backend.pluginDirectory = config.backend.pluginDirectory.lexically_normal();

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

		const auto& queueCapacity = executor.at("queueCapacity");
		const auto stageQueueCapacity = executor.value("stageQueueCapacity", nlohmann::json(1));
		if (!queueCapacity.is_number_integer() || !stageQueueCapacity.is_number_integer() ||
			queueCapacity.get<std::int64_t>() <= 0 || stageQueueCapacity.get<std::int64_t>() <= 0) {
			return invalidConfig(core::StatusCode::InvalidArgument,
				"executor queue capacities must be positive integers");
		}
		config.executor.queueCapacity = queueCapacity.get<std::size_t>();
		config.executor.stageQueueCapacity = stageQueueCapacity.get<std::size_t>();
		return core::Result<DeploymentConfig>::success(config);
	} catch (const std::exception& exception) {
		return invalidConfig(core::StatusCode::InvalidArgument,
			std::string("invalid deployment config: ") + exception.what());
	}
}

} // namespace visionRuntime::config