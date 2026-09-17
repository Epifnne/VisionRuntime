include_guard(GLOBAL)

function(vision_target_runtime targetName)
	if(NOT TARGET ${targetName})
		set(defaultSource "${CMAKE_CURRENT_SOURCE_DIR}/${targetName}.cpp")
		if(NOT EXISTS "${defaultSource}")
			message(FATAL_ERROR
				"vision_target_runtime(${targetName}): target does not exist and "
				"default source was not found at '${defaultSource}'")
		endif()
		add_executable(${targetName} "${defaultSource}")
	endif()
	get_target_property(isImported ${targetName} IMPORTED)
	if(isImported)
		message(FATAL_ERROR
			"vision_target_runtime(${targetName}): imported targets cannot be configured")
	endif()
	get_target_property(alreadyBound ${targetName} VISION_RUNTIME_BOUND)
	if(alreadyBound)
		message(FATAL_ERROR
			"vision_target_runtime(${targetName}): target is already bound to a runtime")
	endif()

	set(oneValueArgs CAMERA)
	set(multiValueArgs BACKEND_PLUGINS)
	cmake_parse_arguments(VISION "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
	if(VISION_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR
			"vision_target_runtime(${targetName}): unknown arguments: ${VISION_UNPARSED_ARGUMENTS}")
	endif()
	if(NOT VISION_CAMERA)
		set(VISION_CAMERA NONE)
	endif()

	set(supportedCameras NONE HIK_MVS)
	if(NOT VISION_CAMERA IN_LIST supportedCameras)
		message(FATAL_ERROR
			"Unsupported camera '${VISION_CAMERA}'. Expected one of: ${supportedCameras}")
	endif()

	if(VISION_CAMERA STREQUAL VISION_CAMERA_SDK)
		set(runtimeTarget visionRuntime)
	else()
		string(TOLOWER "${VISION_CAMERA}" runtimeSuffix)
		string(MAKE_C_IDENTIFIER "${runtimeSuffix}" runtimeSuffix)
		set(runtimeTarget "visionRuntime_${runtimeSuffix}")
		if(NOT TARGET ${runtimeTarget})
			vision_add_runtime(${runtimeTarget} CAMERA ${VISION_CAMERA})
		endif()
	endif()

	target_link_libraries(${targetName} PRIVATE ${runtimeTarget})
	set_property(TARGET ${targetName} PROPERTY VISION_RUNTIME_BOUND TRUE)

	foreach(backend IN LISTS VISION_BACKEND_PLUGINS)
		string(TOLOWER "${backend}" backendId)
		set(pluginTarget "visionBackend${backend}")
		if(NOT TARGET ${pluginTarget})
			message(FATAL_ERROR
				"Backend plugin '${backend}' is not built; enable its VISION_BUILD_*_PLUGIN option")
		endif()
		add_dependencies(${targetName} ${pluginTarget})
		add_custom_command(TARGET ${targetName} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"$<TARGET_FILE_DIR:${pluginTarget}>"
				"$<TARGET_FILE_DIR:${targetName}>/plugins/${backendId}"
			VERBATIM
		)
	endforeach()

	if(WIN32 AND VISION_CAMERA STREQUAL "HIK_MVS")
		_vision_deploy_hik_mvs_runtime(${targetName})
	endif()
endfunction()

function(_vision_deploy_hik_mvs_runtime targetName)
	get_target_property(runtimeDirectory
		VisionHikMvs VISION_HIK_MVS_RUNTIME_DIRECTORY)
	if(NOT runtimeDirectory OR NOT EXISTS "${runtimeDirectory}/MvCameraControl.dll")
		message(FATAL_ERROR
			"Required Hikrobot MVS runtime was not found: ${runtimeDirectory}")
	endif()
	add_custom_command(TARGET ${targetName} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${runtimeDirectory}" "$<TARGET_FILE_DIR:${targetName}>"
		VERBATIM
	)
endfunction()