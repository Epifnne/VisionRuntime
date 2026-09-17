cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0091)
	cmake_policy(SET CMP0091 NEW)
endif()
# Must match the VisionRuntime static libs (dynamic CRT, see root CMakeLists).
set(CMAKE_MSVC_RUNTIME_LIBRARY
	"MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(AnomalyTensorRtMultiSample LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Builds inside the VisionRuntime tree; the sample assembles the pipeline by
# hand (anomalyHikMvsSample style) while profile.json/deployment.json drive
# the product parameters.
if(NOT TARGET visionBackendTensorRt)
	message(FATAL_ERROR
		"anomalyTensorRtMulti must be built from the VisionRuntime root with "
		"-DVISION_BUILD_TENSORRT_PLUGIN=ON -DVISION_BUILD_SAMPLES=ON")
endif()

# The in-tree runtime target carries the include directories publicly.
add_executable(anomalyTensorRtMultiSample
	anomalyTensorRtMultiSample.cpp
)

target_link_libraries(anomalyTensorRtMultiSample PRIVATE
	visionRuntime
	nlohmann_json::nlohmann_json
)

add_dependencies(anomalyTensorRtMultiSample visionBackendTensorRt)

# Self-contained run directory beside the executable: profile + deployment +
# model + images + backend plugin.
set(SAMPLE_RUN_DIR "$<TARGET_FILE_DIR:anomalyTensorRtMultiSample>")
add_custom_command(TARGET anomalyTensorRtMultiSample POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy
		"${CMAKE_CURRENT_SOURCE_DIR}/profile.json" "${SAMPLE_RUN_DIR}/profile.json"
	COMMAND ${CMAKE_COMMAND} -E copy
		"${CMAKE_CURRENT_SOURCE_DIR}/deployment.json" "${SAMPLE_RUN_DIR}/deployment.json"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/model" "${SAMPLE_RUN_DIR}/model"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_CURRENT_SOURCE_DIR}/image" "${SAMPLE_RUN_DIR}/image"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"$<TARGET_FILE_DIR:visionBackendTensorRt>"
		"${SAMPLE_RUN_DIR}/plugins/tensorrt"
	VERBATIM
)

# Deploy TensorRT + CUDA runtime DLLs next to the plugin (the engine loads
# nvinfer/nvinfer_plugin/cudart at plugin load time).
if(WIN32)
	get_target_property(_trtRuntimeFiles VisionTensorRt VISION_TENSORRT_RUNTIME_FILES)
	if(_trtRuntimeFiles)
		foreach(_dll IN LISTS _trtRuntimeFiles)
			add_custom_command(TARGET anomalyTensorRtMultiSample POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy
					"${_dll}" "${SAMPLE_RUN_DIR}/plugins/tensorrt"
				VERBATIM
			)
		endforeach()
	endif()
endif()
