#pragma once
#include "../common/sampleRuntimeOptions.hpp"

namespace visionSample {
visionRuntime::core::Result<std::unique_ptr<visionRuntime::runtime::RuntimeSession<
	visionRuntime::vision::AnomalyResult>>> createSampleRuntime(RuntimeOptions options);
}
