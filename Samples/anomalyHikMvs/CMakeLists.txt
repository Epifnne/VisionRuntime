cmake_minimum_required(VERSION 3.25)

project(AnomalyHikMvsSample LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(VISION_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/VisionRuntime"
	CACHE PATH "Path to the VisionRuntime source package")

if(NOT EXISTS "${VISION_RUNTIME_ROOT}/CMakeLists.txt")
	message(FATAL_ERROR "VisionRuntime path error: '${VISION_RUNTIME_ROOT}' does not exist.")
endif()

add_subdirectory("${VISION_RUNTIME_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/Thirdparty/VisionRuntime")

vision_target_runtime(anomalyHikMvsSample
	CAMERA HIK_MVS
	PLATFORM OPENVINO_INTEL
	DEVICE CPU
	ARTIFACT ONNX
)

target_include_directories(anomalyHikMvsSample PRIVATE
	"${VISION_RUNTIME_ROOT}/Runtime"
)
