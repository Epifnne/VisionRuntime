cmake_minimum_required(VERSION 3.25)

project(VisionRuntime LANGUAGES CXX)

if(NOT MINGW AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	message(FATAL_ERROR "VisionRuntime currently supports MinGW and Linux only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(PROJECT_IS_TOP_LEVEL)
	include(CTest)
	find_package(Git REQUIRED)

	add_custom_target(bootstrapDependencies
		COMMAND ${CMAKE_COMMAND}
			-DGIT_EXECUTABLE=${GIT_EXECUTABLE}
			-DTHIRDPARTY_DIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty
			-P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/bootstrapDependencies.cmake
		VERBATIM
	)
endif()

set(VISION_CAMERA_SDK "NONE" CACHE STRING
	"Camera SDK compiled into VisionRuntime (NONE or HIK_MVS)")
set_property(CACHE VISION_CAMERA_SDK PROPERTY STRINGS NONE HIK_MVS)
set(VISION_INFERENCE_PLATFORM "NONE" CACHE STRING
	"Inference platform compiled into VisionRuntime (NONE or OPENVINO_INTEL)")
set_property(CACHE VISION_INFERENCE_PLATFORM PROPERTY STRINGS NONE OPENVINO_INTEL)
set(VISION_OPENVINO_DEVICE "CPU" CACHE STRING
	"OpenVINO device packaged with the runtime (CPU, GPU, or NPU)")
set_property(CACHE VISION_OPENVINO_DEVICE PROPERTY STRINGS CPU GPU NPU)
set(VISION_MODEL_ARTIFACT_TYPE "ONNX" CACHE STRING
	"Model artifact frontend packaged with the runtime (ONNX or IR)")
set_property(CACHE VISION_MODEL_ARTIFACT_TYPE PROPERTY STRINGS ONNX IR)

if(VISION_INFERENCE_PLATFORM STREQUAL "OPENVINO_INTEL")
	if(NOT VISION_OPENVINO_DEVICE MATCHES "^(CPU|GPU|NPU)$")
		message(FATAL_ERROR
			"VISION_OPENVINO_DEVICE must be CPU, GPU, or NPU")
	endif()
	if(NOT VISION_MODEL_ARTIFACT_TYPE MATCHES "^(ONNX|IR)$")
		message(FATAL_ERROR
			"VISION_MODEL_ARTIFACT_TYPE must be ONNX or IR")
	endif()
endif()

set(BUILD_LIST core,imgcodecs,imgproc CACHE STRING "OpenCV modules required by VisionRuntime" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_java OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_python_bindings_generator OFF CACHE BOOL "" FORCE)
set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
set(WITH_GSTREAMER OFF CACHE BOOL "" FORCE)
set(WITH_IPP OFF CACHE BOOL "" FORCE)
set(WITH_ADE OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/opencv/4.12.0 EXCLUDE_FROM_ALL)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/gtest/1.17.0 EXCLUDE_FROM_ALL)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/json/3.12.0 EXCLUDE_FROM_ALL)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/spdlog/1.15.3 EXCLUDE_FROM_ALL)

add_subdirectory(Runtime)
if(PROJECT_IS_TOP_LEVEL)
	add_subdirectory(Runtime/tests)
	add_subdirectory(Runtime/tools)
endif()
