#pragma once

#include "backends/iInferenceBackend.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace visionRuntime::backends {

struct TensorRtBackendOptions {
	std::filesystem::path enginePath;
	std::string inputName = "image";
	std::string outputName = "score";
	int deviceIndex = 0;
	std::size_t optimizationProfile = 0;
	std::size_t outputBufferCount = 2;
};

class TensorRtBackend final : public IInferenceBackend {
public:
	[[nodiscard]] static core::Result<std::unique_ptr<TensorRtBackend>> create(
		TensorRtBackendOptions options);

	~TensorRtBackend() override;

	TensorRtBackend(const TensorRtBackend&) = delete;
	TensorRtBackend& operator=(const TensorRtBackend&) = delete;
	TensorRtBackend(TensorRtBackend&&) = delete;
	TensorRtBackend& operator=(TensorRtBackend&&) = delete;

	[[nodiscard]] core::Result<preprocess::TensorMap> infer(
		const preprocess::TensorMap& inputs) override;

private:
	class Impl;

	TensorRtBackend(TensorRtBackendOptions options, std::unique_ptr<Impl> impl);

	TensorRtBackendOptions options_;
	std::unique_ptr<Impl> impl_;
};

} // namespace visionRuntime::backends