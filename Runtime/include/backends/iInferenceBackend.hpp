#pragma once

#include "core/result.hpp"
#include "preProcess/preparedInput.hpp"

namespace visionRuntime::backends {

class IInferenceBackend {
public:
	virtual ~IInferenceBackend() = default;

	[[nodiscard]] virtual core::Result<preprocess::TensorMap> infer(
		const preprocess::TensorMap& inputs) = 0;
};

} // namespace visionRuntime::backends