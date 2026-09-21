cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()
# Must match the VisionRuntime static libs (dynamic CRT, see root CMakeLists).
set(CMAKE_MSVC_RUNTIME_LIBRARY
	"MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(AnomalyDirectorySample LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(VISION_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/VisionRuntime"
	CACHE PATH "Path to the VisionRuntime source package")

if(NOT EXISTS "${VISION_RUNTIME_ROOT}/CMakeLists.txt")
	message(FATAL_ERROR "VisionRuntime path error: '${VISION_RUNTIME_ROOT}' does not exist.")
endif()

add_subdirectory("${VISION_RUNTIME_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/Thirdparty/VisionRuntime")

vision_target_runtime(anomalyDirectorySample
	CAMERA NONE
	BACKEND_PLUGINS TensorRt
)

set(ANOMALY_DIRECTORY_PUBLISH_DIR "${CMAKE_CURRENT_BINARY_DIR}/release"
	CACHE PATH "Self-contained release directory for anomalyDirectorySample")

add_custom_target(publish
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${ANOMALY_DIRECTORY_PUBLISH_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${ANOMALY_DIRECTORY_PUBLISH_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy
		"$<TARGET_FILE:anomalyDirectorySample>" "${ANOMALY_DIRECTORY_PUBLISH_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"$<TARGET_FILE_DIR:anomalyDirectorySample>/plugins"
		"${ANOMALY_DIRECTORY_PUBLISH_DIR}/plugins"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/model" "${ANOMALY_DIRECTORY_PUBLISH_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/image" "${ANOMALY_DIRECTORY_PUBLISH_DIR}/image"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/deployment.json"
		"${ANOMALY_DIRECTORY_PUBLISH_DIR}/deployment.json"
	DEPENDS anomalyDirectorySample
	VERBATIM
)

set_property(TARGET publish PROPERTY FOLDER "Custom")
