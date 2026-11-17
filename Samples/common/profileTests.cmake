function(vision_add_profile_test sample mode profile)
	if(TARGET ${sample}Sample)
		set(name "profile.${sample}")
		if(NOT mode STREQUAL "")
			string(APPEND name ".${mode}")
		endif()
		add_test(NAME "${name}"
			COMMAND ${CMAKE_COMMAND}
				"-DCLI=$<TARGET_FILE:${sample}Sample>"
				"-DSAMPLE=${sample}"
				"-DMODE=${mode}"
				"-DPROFILE=${PROJECT_SOURCE_DIR}/Samples/${sample}/${profile}"
				"-DPLUGIN=$<TARGET_FILE:fakeBackendPlugin>"
				"-DSUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX}"
				"-DTEST_DIRECTORY=${CMAKE_BINARY_DIR}/profile-tests/${name}"
				-P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/profileTest.cmake")
		set_tests_properties("${name}" PROPERTIES TIMEOUT 30)
		if(mode STREQUAL "cuda")
			set_tests_properties("${name}" PROPERTIES LABELS "opencv-cuda")
		endif()
		add_dependencies(${sample}Sample fakeBackendPlugin)
	endif()
endfunction()
foreach(sample IN ITEMS anomalyDirectory businessExports eventInspection yoloRouting)
	vision_add_profile_test("${sample}" "" profile.json)
endforeach()
vision_add_profile_test(anomalyDirectory batch profile-batch.json)
if(VISION_WITH_OPENCV_CUDA)
	vision_add_profile_test(businessExports cuda profile-cuda.json)
else()
	vision_add_profile_test(businessExports cudaUnavailable profile-cuda.json)
endif()
