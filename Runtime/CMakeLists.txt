include(CMakeParseArguments)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/vendorTargets.cmake)

set(VISION_RUNTIME_SOURCE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	CACHE INTERNAL "VisionRuntime source directory" FORCE)
set(VISION_RUNTIME_BINARY_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
	CACHE INTERNAL "VisionRuntime binary directory" FORCE)
get_filename_component(VISION_RUNTIME_REPOSITORY_DIRECTORY
	"${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
set(VISION_RUNTIME_REPOSITORY_DIRECTORY "${VISION_RUNTIME_REPOSITORY_DIRECTORY}"
	CACHE INTERNAL "VisionRuntime repository directory" FORCE)

function(vision_add_runtime targetName)
	set(oneValueArgs CAMERA PLATFORM)
	cmake_parse_arguments(VISION "" "${oneValueArgs}" "" ${ARGN})

	if(VISION_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR
			"vision_add_runtime(${targetName}): unknown arguments: ${VISION_UNPARSED_ARGUMENTS}")
	endif()
	if(NOT VISION_CAMERA)
		message(FATAL_ERROR "vision_add_runtime(${targetName}): CAMERA is required")
	endif()
	if(NOT VISION_PLATFORM)
		message(FATAL_ERROR "vision_add_runtime(${targetName}): PLATFORM is required")
	endif()

	set(supportedCameras NONE HIK_MVS)
	if(NOT VISION_CAMERA IN_LIST supportedCameras)
		message(FATAL_ERROR
			"Unsupported camera SDK '${VISION_CAMERA}'. Expected one of: ${supportedCameras}")
	endif()
	set(supportedPlatforms NONE OPENVINO_INTEL)
	if(NOT VISION_PLATFORM IN_LIST supportedPlatforms)
		message(FATAL_ERROR
			"Unsupported inference platform '${VISION_PLATFORM}'. Expected one of: ${supportedPlatforms}")
	endif()

	set(VISION_CAMERA_ENUM "CameraSdk::None")
	set(VISION_CAMERA_TYPE "NoCamera")
	set(VISION_CAMERA_NAME "none")
	set(VISION_CAMERA_SUPPORTS_HARDWARE_TRIGGER false)
	set(VISION_CAMERA_SUPPORTS_SDK_BUFFER_LEASE false)
	set(VISION_CAMERA_SUPPORTS_USER_BUFFERS false)
	if(VISION_CAMERA STREQUAL "HIK_MVS")
		vision_add_hik_mvs_target()
		set(VISION_CAMERA_ENUM "CameraSdk::HikMvs")
		set(VISION_CAMERA_TYPE "HikMvsCamera")
		set(VISION_CAMERA_NAME "hik-mvs")
		set(VISION_CAMERA_SUPPORTS_SDK_BUFFER_LEASE true)
	endif()

	set(VISION_PLATFORM_ENUM "InferencePlatform::None")
	set(VISION_PLATFORM_TYPE "NoInferencePlatform")
	set(VISION_PLATFORM_NAME "none")
	set(VISION_PLATFORM_SUPPORTS_CPU false)
	set(VISION_PLATFORM_SUPPORTS_GPU false)
	set(VISION_PLATFORM_SUPPORTS_NPU false)
	if(VISION_PLATFORM STREQUAL "OPENVINO_INTEL")
		vision_add_openvino_target()
		set(VISION_PLATFORM_ENUM "InferencePlatform::OpenVinoIntel")
		set(VISION_PLATFORM_TYPE "OpenVinoIntelPlatform")
		set(VISION_PLATFORM_NAME "openvino-intel")
		set(VISION_PLATFORM_SUPPORTS_CPU true)
		set(VISION_PLATFORM_SUPPORTS_GPU true)
		set(VISION_PLATFORM_SUPPORTS_NPU true)
	endif()

	set(generatedIncludeDirectory
		"${VISION_RUNTIME_BINARY_DIRECTORY}/generated/${targetName}/include")
	configure_file(
		${VISION_RUNTIME_SOURCE_DIRECTORY}/cmake/buildProfile.hpp.in
		${generatedIncludeDirectory}/config/buildProfile.hpp
		@ONLY
	)

	add_library(${targetName}
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/config/configLoader.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/camera/continuousCameraSource.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/camera/fileSource.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/camera/frameSourceFactory.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/camera/timedTriggerSource.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/postProcess/anomalyPostprocessor.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/postProcess/anomalyThresholdPostprocessor.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/preProcess/frameNodes/cvCenterCropNode.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/preProcess/frameNodes/cvResizeNode.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/preProcess/frameNodes/toTensorNode.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/preProcess/preprocessChain.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/preProcess/tensorNodes/normalizeNode.cpp
	)
	target_compile_features(${targetName} PUBLIC cxx_std_20)
	if(MSVC)
		target_compile_options(${targetName} PRIVATE /W4 /permissive-)
	else()
		target_compile_options(${targetName} PRIVATE
			-Wall
			-Wextra
			-Wpedantic
			-Wconversion
			-Wsign-conversion
		)
	endif()

	target_include_directories(${targetName}
		PUBLIC
			${VISION_RUNTIME_SOURCE_DIRECTORY}/includeModules
			${VISION_RUNTIME_SOURCE_DIRECTORY}/include
			${generatedIncludeDirectory}
	)
	target_include_directories(${targetName} SYSTEM PRIVATE
			${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/opencv/4.12.0/modules/core/include
			${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/opencv/4.12.0/modules/imgcodecs/include
			${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/opencv/4.12.0/modules/imgproc/include
			${CMAKE_BINARY_DIR}
	)

	target_link_libraries(${targetName}
		PRIVATE
			nlohmann_json::nlohmann_json
			opencv_imgcodecs
			opencv_imgproc
	)
	if(VISION_PLATFORM STREQUAL "OPENVINO_INTEL")
		target_sources(${targetName} PRIVATE
			${VISION_RUNTIME_SOURCE_DIRECTORY}/src/backends/openVinoBackend.cpp
		)
		target_link_libraries(${targetName} PRIVATE Vision::OpenVino)
	endif()
	if(VISION_CAMERA STREQUAL "HIK_MVS")
		target_sources(${targetName} PRIVATE
			${VISION_RUNTIME_SOURCE_DIRECTORY}/src/camera/hikrobotMvsCameraDevice.cpp
		)
		target_link_libraries(${targetName} PRIVATE Vision::HikMvs)
	endif()
endfunction()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/visionTargetRuntime.cmake)

vision_add_runtime(visionRuntime
	CAMERA ${VISION_CAMERA_SDK}
	PLATFORM ${VISION_INFERENCE_PLATFORM}
)