set(VISION_BACKEND_PLUGIN_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(vision_configure_backend_plugin targetName backendId)
	target_include_directories(${targetName} PRIVATE
		"${VISION_RUNTIME_SOURCE_DIRECTORY}/include"
		"${VISION_BACKEND_PLUGIN_SOURCE_DIR}")
	target_compile_features(${targetName} PRIVATE cxx_std_20)
	target_compile_definitions(${targetName} PRIVATE VISION_RUNTIME_BACKEND_PLUGIN_EXPORTS)
	target_link_libraries(${targetName} PRIVATE nlohmann_json::nlohmann_json)
	if(MSVC)
		target_link_options(${targetName} PRIVATE /INCREMENTAL:NO)
		set_target_properties(${targetName} PROPERTIES
			PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb")
	endif()
	if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
		target_link_options(${targetName} PRIVATE
			"LINKER:--version-script=${VISION_BACKEND_PLUGIN_SOURCE_DIR}/exports.map")
		set_property(TARGET ${targetName} APPEND PROPERTY LINK_DEPENDS
			"${VISION_BACKEND_PLUGIN_SOURCE_DIR}/exports.map")
	endif()
	set_target_properties(${targetName} PROPERTIES
		PREFIX ""
		OUTPUT_NAME "vision-backend-${backendId}"
		CXX_VISIBILITY_PRESET hidden
		VISIBILITY_INLINES_HIDDEN YES
		BUILD_RPATH "$ORIGIN"
		INSTALL_RPATH "$ORIGIN"
		BUILD_WITH_INSTALL_RPATH YES
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${backendId}"
		LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/${backendId}"
	)
	install(TARGETS ${targetName}
		RUNTIME DESTINATION "plugins/${backendId}" COMPONENT BackendPlugins
		LIBRARY DESTINATION "plugins/${backendId}" COMPONENT BackendPlugins)
endfunction()

function(vision_deploy_backend_libraries targetName backendId)
	foreach(runtimeFile IN LISTS ARGN)
		add_custom_command(TARGET ${targetName} POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${runtimeFile}" "$<TARGET_FILE_DIR:${targetName}>" VERBATIM)
		install(FILES "${runtimeFile}" DESTINATION "plugins/${backendId}" COMPONENT BackendPlugins)
	endforeach()
endfunction()

add_subdirectory(openvino)
add_subdirectory(tensorrt)