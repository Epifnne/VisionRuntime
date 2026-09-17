cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()
# Dynamic CRT (/MD) is mandatory: visionShell links prebuilt Qt (/MD), and a
# static CRT here would give the process two separate CRT heaps (ucrt
# debug_heap __acrt_first_block assert on any cross-boundary free).
set(CMAKE_MSVC_RUNTIME_LIBRARY
	"MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(VisionRuntime LANGUAGES CXX)

if(NOT MINGW AND NOT MSVC AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	message(FATAL_ERROR "VisionRuntime currently supports MSVC, MinGW, and Linux only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
if(VISION_BUILD_SHELL)
	if(NOT VISION_BUILD_SERVICE)
		message(FATAL_ERROR "VISION_BUILD_SHELL requires VISION_BUILD_SERVICE=ON")
	endif()
	add_subdirectory(Shell)
endif()
if(PROJECT_IS_TOP_LEVEL)
	add_subdirectory(Runtime/tests)
	if(VISION_BUILD_SERVICE)
		add_subdirectory(Service/tests)
	endif()
	add_subdirectory(Runtime/tools)
	if(VISION_BUILD_SAMPLES)
		add_subdirectory(Samples/anomalyTensorRtMulti)
	endif()
endif()
