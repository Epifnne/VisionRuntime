#pragma once
#include "../common/sampleRuntimeOptions.hpp"
#include "session/inspectionSession.hpp"

namespace visionSample {
visionRuntime::core::Result<std::unique_ptr<visionService::session::IInspectionSession>>
createSampleRuntime(RuntimeOptions options);
}
