#pragma once

#include "backends/iInferenceBackend.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace visionRuntime::backends {

struct OpenVinoBackendOptions {
	std::filesystem::path modelPath;
	std::string device = "CPU";
	std::string inputName = "image";
	std::string outputName = "score";
	std::size_t inferenceThreads = 0;
	std::size_t outputBufferCount = 2;
};

class OpenVinoBackend final : public IInferenceBackend {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<OpenVinoBackend>> create(
		OpenVinoBackendOptions options);

	~OpenVinoBackend() override;

	OpenVinoBackend(const OpenVinoBackend&) = delete;
	OpenVinoBackend& operator=(const OpenVinoBackend&) = delete;
	OpenVinoBackend(OpenVinoBackend&&) = delete;
	OpenVinoBackend& operator=(OpenVinoBackend&&) = delete;

	[[nodiscard]] core::Result<preprocess::TensorMap> infer(
		const preprocess::TensorMap& inputs) override;

private:
	class Impl;

	OpenVinoBackend(OpenVinoBackendOptions options, std::unique_ptr<Impl> impl);

	OpenVinoBackendOptions options_;
	std::unique_ptr<Impl> impl_;
};

} // namespace visionRuntime::backends