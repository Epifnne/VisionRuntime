#pragma once

#include "backends/iInferenceBackend.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace visionRuntime::backends {

[[nodiscard]] std::filesystem::path backendPluginPath(
	const std::filesystem::path& pluginDirectory, std::string_view backendId);

struct PluginBackendOptions {
	std::filesystem::path pluginPath;
	std::string backendId;
	std::filesystem::path artifactPath;
	std::string device;
	std::string optionsJson;
	std::string artifactKind;
	std::string inputName;
	std::string outputName;
};

class PluginInferenceBackend final : public IInferenceBackend {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<PluginInferenceBackend>> create(
		PluginBackendOptions options);

	~PluginInferenceBackend() override;

	PluginInferenceBackend(const PluginInferenceBackend&) = delete;
	PluginInferenceBackend& operator=(const PluginInferenceBackend&) = delete;
	PluginInferenceBackend(PluginInferenceBackend&&) = delete;
	PluginInferenceBackend& operator=(PluginInferenceBackend&&) = delete;

	[[nodiscard]] core::Result<preprocess::TensorMap> infer(
		const preprocess::TensorMap& inputs) override;

private:
	class Impl;
	explicit PluginInferenceBackend(std::unique_ptr<Impl> impl);

	[[nodiscard]] static core::Result<std::unique_ptr<PluginInferenceBackend>>
	createUnchecked(PluginBackendOptions options);

	std::unique_ptr<Impl> impl_;
};

} // namespace visionRuntime::backends
