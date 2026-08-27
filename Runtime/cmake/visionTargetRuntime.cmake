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

	set(oneValueArgs PLATFORM DEVICE ARTIFACT CAMERA)
	cmake_parse_arguments(VISION "" "${oneValueArgs}" "" ${ARGN})
	if(VISION_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR
			"vision_target_runtime(${targetName}): unknown arguments: ${VISION_UNPARSED_ARGUMENTS}")
	endif()
	if(NOT VISION_PLATFORM)
		message(FATAL_ERROR
			"vision_target_runtime(${targetName}): PLATFORM is required")
	endif()
	if(NOT VISION_CAMERA)
		set(VISION_CAMERA NONE)
	endif()

	set(supportedPlatforms NONE OPENVINO_INTEL)
	set(supportedCameras NONE HIK_MVS)
	if(NOT VISION_PLATFORM IN_LIST supportedPlatforms)
		message(FATAL_ERROR
			"Unsupported platform '${VISION_PLATFORM}'. Expected one of: ${supportedPlatforms}")
	endif()
	if(NOT VISION_CAMERA IN_LIST supportedCameras)
		message(FATAL_ERROR
			"Unsupported camera '${VISION_CAMERA}'. Expected one of: ${supportedCameras}")
	endif()

	if(VISION_PLATFORM STREQUAL "OPENVINO_INTEL")
		if(NOT VISION_DEVICE MATCHES "^(CPU|GPU|NPU)$")
			message(FATAL_ERROR
				"vision_target_runtime(${targetName}): DEVICE must be CPU, GPU, or NPU")
		endif()
		if(NOT VISION_ARTIFACT MATCHES "^(ONNX|IR)$")
			message(FATAL_ERROR
				"vision_target_runtime(${targetName}): ARTIFACT must be ONNX or IR")
		endif()
	elseif(VISION_DEVICE OR VISION_ARTIFACT)
		message(FATAL_ERROR
			"vision_target_runtime(${targetName}): DEVICE and ARTIFACT require OPENVINO_INTEL")
	endif()

	if(VISION_PLATFORM STREQUAL VISION_INFERENCE_PLATFORM AND
		VISION_CAMERA STREQUAL VISION_CAMERA_SDK)
		set(runtimeTarget visionRuntime)
	else()
		string(TOLOWER "${VISION_PLATFORM}_${VISION_CAMERA}" runtimeSuffix)
		string(MAKE_C_IDENTIFIER "${runtimeSuffix}" runtimeSuffix)
		set(runtimeTarget "visionRuntime_${runtimeSuffix}")
		if(NOT TARGET ${runtimeTarget})
			vision_add_runtime(${runtimeTarget}
				CAMERA ${VISION_CAMERA}
				PLATFORM ${VISION_PLATFORM}
			)
		endif()
	endif()

	target_link_libraries(${targetName} PRIVATE ${runtimeTarget})
	set_property(TARGET ${targetName} PROPERTY VISION_RUNTIME_BOUND TRUE)

	if(VISION_PLATFORM STREQUAL "OPENVINO_INTEL")
		target_compile_definitions(${targetName} PRIVATE
			VISION_RUNTIME_DEVICE_NAME="${VISION_DEVICE}"
			VISION_RUNTIME_ARTIFACT_TYPE="${VISION_ARTIFACT}"
		)
		if(WIN32)
			_vision_deploy_openvino_runtime(${targetName} ${VISION_DEVICE} ${VISION_ARTIFACT})
		endif()
	endif()
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

function(_vision_deploy_openvino_runtime targetName device artifact)
	set(runtimeDlls openvino_c.dll openvino.dll tbb12.dll)
	if(device STREQUAL "CPU")
		list(APPEND runtimeDlls openvino_intel_cpu_plugin.dll)
	elseif(device STREQUAL "GPU")
		list(APPEND runtimeDlls openvino_intel_gpu_plugin.dll)
	elseif(device STREQUAL "NPU")
		list(APPEND runtimeDlls
			openvino_intel_npu_plugin.dll
			openvino_intel_npu_compiler_loader.dll
			openvino_intel_npu_compiler.dll
		)
	endif()

	if(artifact STREQUAL "ONNX")
		list(APPEND runtimeDlls openvino_onnx_frontend.dll)
	else()
		list(APPEND runtimeDlls openvino_ir_frontend.dll)
	endif()

	foreach(runtimeDll IN LISTS runtimeDlls)
		set(runtimeDllPath "${VISION_OPENVINO_RUNTIME_DIRECTORY}/${runtimeDll}")
		if(NOT EXISTS "${runtimeDllPath}")
			message(FATAL_ERROR
				"Required OpenVINO runtime DLL was not found: ${runtimeDllPath}")
		endif()
		add_custom_command(TARGET ${targetName} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${runtimeDllPath}" "$<TARGET_FILE_DIR:${targetName}>"
			VERBATIM
		)
	endforeach()

	if(MINGW)
		get_filename_component(mingwBinDirectory "${CMAKE_CXX_COMPILER}" DIRECTORY)
		foreach(runtimeDll IN ITEMS libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
			set(runtimeDllPath "${mingwBinDirectory}/${runtimeDll}")
			if(NOT EXISTS "${runtimeDllPath}")
				message(FATAL_ERROR
					"Required MinGW runtime DLL was not found: ${runtimeDllPath}")
			endif()
			add_custom_command(TARGET ${targetName} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${runtimeDllPath}" "$<TARGET_FILE_DIR:${targetName}>"
				VERBATIM
			)
		endforeach()
	endif()
endfunction()