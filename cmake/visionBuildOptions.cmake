include_guard(GLOBAL)

option(VISION_BUILD_OPENVINO_PLUGIN "Build the OpenVINO C ABI plugin" OFF)
option(VISION_BUILD_TENSORRT_PLUGIN "Build the TensorRT C ABI plugin" OFF)
option(VISION_BUILD_SERVICE "Build the visionService application service library" OFF)
option(VISION_BUILD_SHELL "Build the visionShell Qt Quick executable" OFF)
option(VISION_BUILD_DESIGNER "Build the visionDesigner UI composition tool" OFF)
option(VISION_BUILD_SAMPLES "Build the in-tree Samples examples" OFF)
option(VISION_WITH_OPENCV_CUDA "Build OpenCV CUDA image-processing modules" OFF)

set(VISION_OPENCV_CONTRIB_ROOT
	"${CMAKE_CURRENT_LIST_DIR}/../Thirdparty/opencv_contrib/4.14.0"
	CACHE PATH "OpenCV contrib source directory matching OpenCV 4.14.0")
set(VISION_OPENCV_CUDA_ARCH_BIN "" CACHE STRING
	"CUDA compute capabilities for OpenCV, e.g. 8.6 or 8.6;8.9")

set(VISION_CAMERA_SDK "NONE" CACHE STRING
	"Camera SDK compiled into VisionRuntime (NONE or HIK_MVS)")
set_property(CACHE VISION_CAMERA_SDK PROPERTY STRINGS NONE HIK_MVS)
