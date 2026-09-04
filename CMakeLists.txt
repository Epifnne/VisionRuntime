cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY
	"MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(VisionRuntime LANGUAGES CXX)

if(NOT MINGW AND NOT MSVC AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
	message(FATAL_ERROR "VisionRuntime currently supports MSVC, MinGW, and Linux only")
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

include(cmake/visionBuildOptions.cmake)
include(cmake/visionDependencies.cmake)

add_subdirectory(Runtime)
if(PROJECT_IS_TOP_LEVEL)
	add_subdirectory(Runtime/tests)
	add_subdirectory(Runtime/tools)
endif()
