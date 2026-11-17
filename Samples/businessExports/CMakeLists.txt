cmake_minimum_required(VERSION 3.25)
if(NOT TARGET visionRuntime)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	project(MeasurementSample LANGUAGES CXX)
	include(CTest)
	set(VISION_BUILD_SERVICE ON CACHE BOOL "" FORCE)
	set(VISION_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.."
		CACHE PATH "Path to VisionRuntime")
	add_subdirectory("${VISION_RUNTIME_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/VisionRuntime")
endif()

add_library(visionProductApplication STATIC businessApplication.cpp)
add_library(visionProductContract STATIC businessContract.cpp)
target_link_libraries(visionProductContract PUBLIC visionService)
target_link_libraries(visionProductApplication PUBLIC visionProductContract PRIVATE opencv_core)
target_include_directories(visionProductApplication SYSTEM PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../../Thirdparty/opencv/4.14.0/modules/core/include"
	"${CMAKE_BINARY_DIR}")
set_property(TARGET visionProductApplication PROPERTY VISION_PRODUCT_ID businessExports)
set_property(TARGET visionProductApplication PROPERTY VISION_PRODUCT_PROFILE_ASSETS
	"${CMAKE_CURRENT_SOURCE_DIR}/images")

add_executable(businessExportsSample main.cpp)
target_link_libraries(businessExportsSample PRIVATE visionProductApplication)
if(VISION_WITH_OPENCV_CUDA)
	add_library(openCvCudaAlgorithm STATIC cudaThresholdAlgorithm.cpp cudaSelfTest.cpp)
	target_link_libraries(openCvCudaAlgorithm PUBLIC visionRuntime Vision::OpenCvCuda)
	target_include_directories(openCvCudaAlgorithm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	target_compile_features(openCvCudaAlgorithm PUBLIC cxx_std_20)
	target_link_libraries(visionProductApplication PRIVATE openCvCudaAlgorithm)
	target_link_libraries(businessExportsSample PRIVATE openCvCudaAlgorithm)
	target_compile_definitions(visionProductApplication PRIVATE VISION_MEASUREMENT_CUDA=1)
	target_compile_definitions(businessExportsSample PRIVATE VISION_MEASUREMENT_CUDA=1)
	if(BUILD_TESTING)
		include(GoogleTest)
		add_executable(openCvCudaTest cudaThresholdAlgorithmTest.cpp)
		target_link_libraries(openCvCudaTest PRIVATE openCvCudaAlgorithm visionProductApplication GTest::gtest_main)
		target_compile_definitions(openCvCudaTest PRIVATE
			VISION_CUDA_TEST_PROFILE="${CMAKE_CURRENT_SOURCE_DIR}/profile-cuda.json")
		gtest_discover_tests(openCvCudaTest TEST_PREFIX "opencvCuda."
			DISCOVERY_MODE PRE_TEST PROPERTIES LABELS "opencv-cuda")
		add_test(NAME opencvCuda.selfTest COMMAND businessExportsSample --self-test)
		set_tests_properties(opencvCuda.selfTest PROPERTIES LABELS "opencv-cuda")
	endif()
endif()
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
