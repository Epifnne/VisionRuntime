#pragma once

#include "core/result.hpp"
#include "preprocess/preparedInput.hpp"

namespace visonRuntime::backends {

class IInferenceBackend {
public:
	virtual ~IInferenceBackend() = default;

	[[nodiscard]] virtual core::Result<preprocess::TensorMap> infer(
		const preprocess::TensorMap& inputs) = 0;
};

} // namespace visonRuntime::backends