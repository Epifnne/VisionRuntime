include_guard(GLOBAL)

include(CMakeParseArguments)

function(vision_add_runtime targetName)
	set(oneValueArgs CAMERA)
	cmake_parse_arguments(VISION "" "${oneValueArgs}" "" ${ARGN})

	if(VISION_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR
			"vision_add_runtime(${targetName}): unknown arguments: ${VISION_UNPARSED_ARGUMENTS}")
	endif()
	if(NOT VISION_CAMERA)
		message(FATAL_ERROR "vision_add_runtime(${targetName}): CAMERA is required")
	endif()

	set(supportedCameras NONE HIK_MVS)
	if(NOT VISION_CAMERA IN_LIST supportedCameras)
		message(FATAL_ERROR
			"Unsupported camera SDK '${VISION_CAMERA}'. Expected one of: ${supportedCameras}")
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

	set(generatedIncludeDirectory
		"${VISION_RUNTIME_BINARY_DIRECTORY}/generated/${targetName}/include")
	configure_file(
		${VISION_RUNTIME_SOURCE_DIRECTORY}/cmake/buildProfile.hpp.in
		${generatedIncludeDirectory}/config/buildProfile.hpp
		@ONLY
	)

	add_library(${targetName}
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/backends/pluginInferenceBackend.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/config/configLoader.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/config/modelPackageLoader.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/camera/continuousCameraSource.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/camera/fileSource.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/camera/frameSourceFactory.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/camera/timedTriggerSource.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/logs/logger.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/postprocess/anomalyThresholdPostprocessor.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/preprocess/frameNodes/cvCenterCropNode.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/preprocess/frameNodes/cvResizeNode.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/preprocess/frameNodes/toTensorNode.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/preprocess/preprocessChain.cpp
		${VISION_RUNTIME_SOURCE_DIRECTORY}/src/preprocess/tensorNodes/normalizeNode.cpp
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
			${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/spdlog/1.15.3/include
			${CMAKE_BINARY_DIR}
	)

	target_link_libraries(${targetName}
		PRIVATE
			${CMAKE_DL_LIBS}
			nlohmann_json::nlohmann_json
			opencv_imgcodecs
			opencv_imgproc
			spdlog::spdlog
	)
	if(VISION_CAMERA STREQUAL "HIK_MVS")
		target_sources(${targetName} PRIVATE
			${VISION_RUNTIME_SOURCE_DIRECTORY}/src/camera/hikrobotMvsCameraDevice.cpp
		)
		target_link_libraries(${targetName} PRIVATE Vision::HikMvs)
	endif()
endfunction()