include_guard(GLOBAL)

function(vision_add_anomaly_sample sample)
	set(directory "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../${sample}")
	if(TARGET ${sample}Sample)
		return()
	endif()
	add_executable(${sample}Sample
		"${directory}/${sample}Sample.cpp"
		"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/productShell.cpp"
		"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/anomalyMain.cpp")
	target_include_directories(${sample}Sample PRIVATE "${directory}")
	target_compile_definitions(${sample}Sample PRIVATE VISION_PRODUCT_ID="${sample}")
	target_link_libraries(${sample}Sample PRIVATE visionService nlohmann_json::nlohmann_json)
	foreach(backend IN ITEMS OpenVino TensorRt)
		if(TARGET visionBackend${backend})
			add_dependencies(${sample}Sample visionBackend${backend})
		endif()
	endforeach()
	if(WIN32 AND VISION_CAMERA_SDK STREQUAL "HIK_MVS")
		_vision_deploy_hik_mvs_runtime(${sample}Sample)
	endif()
endfunction()
