include_guard(GLOBAL)

option(VISION_BUILD_OPENVINO_PLUGIN "Build the OpenVINO C ABI plugin" OFF)
option(VISION_BUILD_TENSORRT_PLUGIN "Build the TensorRT C ABI plugin" OFF)
option(VISION_BUILD_SERVICE "Build the visionService application service library" OFF)
option(VISION_BUILD_SHELL "Build the visionShell Qt Quick executable" OFF)
option(VISION_BUILD_SAMPLES "Build the in-tree Samples examples" OFF)

set(VISION_CAMERA_SDK "NONE" CACHE STRING
	"Camera SDK compiled into VisionRuntime (NONE or HIK_MVS)")
set_property(CACHE VISION_CAMERA_SDK PROPERTY STRINGS NONE HIK_MVS)
