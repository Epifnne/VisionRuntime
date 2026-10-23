add_library(visionProductApplication STATIC businessApplication.cpp)
add_library(visionProductContract STATIC businessContract.cpp)
target_link_libraries(visionProductContract PUBLIC visionService)
target_link_libraries(visionProductApplication PUBLIC visionProductContract PRIVATE opencv_core)
target_include_directories(visionProductApplication SYSTEM PRIVATE
	"${PROJECT_SOURCE_DIR}/Thirdparty/opencv/4.12.0/modules/core/include"
	"${CMAKE_BINARY_DIR}")
set_property(TARGET visionProductApplication PROPERTY VISION_PRODUCT_ID businessExports)
set_property(TARGET visionProductApplication PROPERTY VISION_PRODUCT_PROFILE_ASSETS
	"${CMAKE_CURRENT_SOURCE_DIR}/images")

add_executable(businessExportsSample main.cpp)
target_link_libraries(businessExportsSample PRIVATE visionProductApplication)
vision_export_product(visionProductApplication
	CONTRACT visionProductContract
	PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/profile.json"
	CLI businessExportsSample)
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
	add_test(NAME businessExportsCli COMMAND businessExportsSample
		"${CMAKE_CURRENT_SOURCE_DIR}/profile.json")
	add_test(NAME businessExportsManifest COMMAND businessExportsSample
		"${CMAKE_CURRENT_SOURCE_DIR}/profile.json" --export-manifest
		"${CMAKE_CURRENT_BINARY_DIR}/exports.json")
	add_test(NAME businessExportsOfflineManifest
		COMMAND ${CMAKE_COMMAND}
			"-DGENERATOR=$<TARGET_FILE:visionProductManifestGenerator>"
			"-DCLI=$<TARGET_FILE:businessExportsSample>"
			"-DPROFILE=${CMAKE_CURRENT_SOURCE_DIR}/profile.json"
			"-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/offline-manifest-test"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/offlineManifestTest.cmake")
endif()
