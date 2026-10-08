#pragma once
#include "../common/sampleRuntimeOptions.hpp"

namespace visionSample {
visionRuntime::core::Result<std::unique_ptr<visionRuntime::runtime::MultiCameraSession<
	visionRuntime::vision::AnomalyResult>>> createSampleRuntime(RuntimeOptions options);
}
