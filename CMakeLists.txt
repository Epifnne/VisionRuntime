cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()
# Dynamic CRT (/MD) is mandatory: visionShell links prebuilt Qt (/MD), and a
# static CRT here would give the process two separate CRT heaps (ucrt
# debug_heap __acrt_first_block assert on any cross-boundary free).
set(CMAKE_MSVC_RUNTIME_LIBRARY
	"MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(VisionRuntime VERSION 1.0.0 LANGUAGES CXX)

if(NOT MINGW AND NOT MSVC AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	message(FATAL_ERROR "VisionRuntime currently supports MSVC, MinGW, and Linux only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(VISION_PRODUCT_SAMPLE "" CACHE STRING "Sample runtime linked into this product Shell")
set(VISION_PRODUCT_APPLICATION "" CACHE PATH "Business project defining a visionProductApplication target")
if(VISION_PRODUCT_SAMPLE AND VISION_PRODUCT_APPLICATION)
	message(FATAL_ERROR "Select VISION_PRODUCT_SAMPLE or VISION_PRODUCT_APPLICATION, not both")
endif()
if(VISION_PRODUCT_SAMPLE)
	include(Samples/common/configureProduct.cmake)
endif()
if(VISION_PRODUCT_APPLICATION AND EXISTS "${VISION_PRODUCT_APPLICATION}/application.cmake")
	include("${VISION_PRODUCT_APPLICATION}/application.cmake")
endif()
include(cmake/visionBuildOptions.cmake)

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

include(cmake/visionDependencies.cmake)

add_subdirectory(Runtime)
if(VISION_BUILD_SERVICE)
	add_subdirectory(Service)
endif()
if(VISION_PRODUCT_APPLICATION)
	if(NOT VISION_BUILD_SERVICE)
		message(FATAL_ERROR "VISION_PRODUCT_APPLICATION requires VISION_BUILD_SERVICE=ON")
	endif()
	add_subdirectory("${VISION_PRODUCT_APPLICATION}" "${CMAKE_CURRENT_BINARY_DIR}/ProductApplication")
	if(NOT TARGET visionProductApplication)
		message(FATAL_ERROR "Business project must define visionProductApplication")
	endif()
	get_target_property(VISION_APPLICATION_ID visionProductApplication VISION_PRODUCT_ID)
	if(NOT VISION_APPLICATION_ID)
		message(FATAL_ERROR "visionProductApplication must declare VISION_PRODUCT_ID")
	endif()
endif()
if(VISION_BUILD_SHELL)
	if(NOT VISION_BUILD_SERVICE)
		message(FATAL_ERROR "VISION_BUILD_SHELL requires VISION_BUILD_SERVICE=ON")
	endif()
	add_subdirectory(Shell)
endif()
# visionDesigner is a standalone profile/manifest editor; it must not link
# Runtime/Service (runs on machines without camera SDKs or inference stacks).
if(VISION_BUILD_DESIGNER)
	add_subdirectory(Tools/uiDesigner)
endif()
if(PROJECT_IS_TOP_LEVEL)
	add_subdirectory(Runtime/tests)
	if(VISION_BUILD_SERVICE)
		add_subdirectory(Service/tests)
	endif()
	add_subdirectory(Runtime/tools)
	if(VISION_BUILD_SAMPLES)
		if(VISION_BUILD_SERVICE AND NOT TARGET eventInspectionSample)
			add_subdirectory(Samples/eventInspection)
		endif()
		if(VISION_BUILD_SERVICE AND NOT TARGET visionProductApplication)
			add_subdirectory(Samples/businessExports)
		endif()
		add_subdirectory(Samples/yoloRouting)
		if(VISION_BUILD_TENSORRT_PLUGIN)
			add_subdirectory(Samples/anomalyTensorRtMulti)
		endif()
	endif()
endif()
