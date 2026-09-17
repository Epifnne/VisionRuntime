if(VISION_BUILD_OPENVINO_PLUGIN)
	vision_add_openvino_target()
	add_library(visionBackendOpenVino SHARED openVinoPlugin.cpp)
	vision_configure_backend_plugin(visionBackendOpenVino openvino)
	target_link_libraries(visionBackendOpenVino PRIVATE Vision::OpenVino)
	if(WIN32)
		set(runtimeFiles "")
		foreach(runtimeFile IN ITEMS openvino_c.dll openvino.dll tbb12.dll
			openvino_intel_cpu_plugin.dll openvino_intel_gpu_plugin.dll
			openvino_intel_npu_plugin.dll openvino_intel_npu_compiler_loader.dll
			openvino_intel_npu_compiler.dll openvino_onnx_frontend.dll openvino_ir_frontend.dll)
			list(APPEND runtimeFiles "${VISION_OPENVINO_RUNTIME_DIRECTORY}/${runtimeFile}")
		endforeach()
		if(MINGW)
			get_filename_component(compilerDirectory "${CMAKE_CXX_COMPILER}" DIRECTORY)
			foreach(runtimeFile IN ITEMS libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
				list(APPEND runtimeFiles "${compilerDirectory}/${runtimeFile}")
			endforeach()
		endif()
		vision_deploy_backend_libraries(visionBackendOpenVino openvino ${runtimeFiles})
	else()
		get_target_property(runtimeLocation openvino::runtime::c IMPORTED_LOCATION_RELEASE)
		get_filename_component(runtimeDirectory "${runtimeLocation}" DIRECTORY)
		file(GLOB runtimeFiles
			"${runtimeDirectory}/libopenvino.so*" "${runtimeDirectory}/libopenvino_c.so*"
			"${runtimeDirectory}/libopenvino_intel_*_plugin.so*"
			"${runtimeDirectory}/libopenvino_ir_frontend.so*"
			"${runtimeDirectory}/libopenvino_onnx_frontend.so*"
			"${runtimeDirectory}/libtbb*.so*" "${runtimeDirectory}/libhwloc.so*")
		vision_deploy_backend_libraries(visionBackendOpenVino openvino ${runtimeFiles})
	endif()
endif()