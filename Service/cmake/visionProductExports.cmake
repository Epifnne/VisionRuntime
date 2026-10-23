function(vision_export_product application)
	cmake_parse_arguments(PARSE_ARGV 1 export "" "CONTRACT;PROFILE;CLI" "")
	if(export_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR "Unknown vision_export_product arguments: ${export_UNPARSED_ARGUMENTS}")
	endif()
	foreach(required CONTRACT PROFILE CLI)
		if(NOT export_${required})
			message(FATAL_ERROR "vision_export_product requires ${required}")
		endif()
	endforeach()
	foreach(target "${application}" "${export_CONTRACT}" "${export_CLI}")
		if(NOT TARGET "${target}")
			message(FATAL_ERROR "vision_export_product requires an existing target: ${target}")
		endif()
	endforeach()
	if(CMAKE_CROSSCOMPILING)
		message(FATAL_ERROR "Product manifest generation currently requires a native build")
	endif()
	get_target_property(productId ${application} VISION_PRODUCT_ID)
	if(NOT productId MATCHES "^[A-Za-z0-9][A-Za-z0-9_.-]*$")
		message(FATAL_ERROR "${application} must declare a valid VISION_PRODUCT_ID")
	endif()
	get_filename_component(profile "${export_PROFILE}" ABSOLUTE)
	add_executable(visionProductManifestGenerator
		"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/productManifestDump.cpp")
	target_link_libraries(visionProductManifestGenerator PRIVATE ${export_CONTRACT})
	set(manifest "${CMAKE_BINARY_DIR}/manifests/$<CONFIG>/${productId}.manifest.json")
	add_custom_command(OUTPUT "${manifest}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/manifests/$<CONFIG>"
		COMMAND "$<TARGET_FILE:visionProductManifestGenerator>" "${profile}" "${manifest}"
		DEPENDS visionProductManifestGenerator "${profile}"
		COMMENT "Describing ${productId} exports without initializing the business runtime"
		VERBATIM)
	add_custom_target(visionProductManifest ALL
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${export_CLI}>"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${manifest}"
			"$<TARGET_FILE_DIR:${export_CLI}>/${productId}.manifest.json"
		DEPENDS "${manifest}"
		VERBATIM)
	add_dependencies(${export_CLI} visionProductManifest)
	set_target_properties(${application} PROPERTIES
		VISION_PRODUCT_PROFILE "${profile}"
		VISION_PRODUCT_CLI "${export_CLI}"
		VISION_PRODUCT_MANIFEST "${manifest}")
endfunction()
