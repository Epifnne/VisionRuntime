foreach(sample IN ITEMS anomalyDirectory anomalyTensorRtMulti
	businessExports eventInspection yoloRouting openCvCuda)
	if(TARGET ${sample}Sample)
		add_test(NAME profile.${sample}
			COMMAND ${CMAKE_COMMAND}
				"-DCLI=$<TARGET_FILE:${sample}Sample>"
				"-DSAMPLE=${sample}"
				"-DPROFILE=${PROJECT_SOURCE_DIR}/Samples/${sample}/profile.json"
				"-DPLUGIN=$<TARGET_FILE:fakeBackendPlugin>"
				"-DSUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX}"
				"-DTEST_DIRECTORY=${CMAKE_BINARY_DIR}/profile-tests/${sample}"
				-P "${CMAKE_CURRENT_LIST_DIR}/profileTest.cmake")
		set_tests_properties(profile.${sample} PROPERTIES TIMEOUT 30)
		if(sample STREQUAL "openCvCuda")
			set_tests_properties(profile.${sample} PROPERTIES LABELS "opencv-cuda")
		endif()
		add_dependencies(${sample}Sample fakeBackendPlugin)
	endif()
endforeach()
