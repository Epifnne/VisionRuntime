cmake_minimum_required(VERSION 3.25)

if(NOT TARGET visionRuntime)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	project(OpenCvCudaSample LANGUAGES CXX)
	include(CTest)
	set(VISION_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.."
		CACHE PATH "Path to the VisionRuntime source package")
	add_subdirectory("${VISION_RUNTIME_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/VisionRuntime")
endif()

if(NOT TARGET Vision::OpenCvCuda)
	message(FATAL_ERROR "openCvCudaSample requires VISION_WITH_OPENCV_CUDA=ON")
endif()

add_library(openCvCudaAlgorithm STATIC cudaThresholdAlgorithm.cpp)
target_link_libraries(openCvCudaAlgorithm PUBLIC visionRuntime Vision::OpenCvCuda)
target_include_directories(openCvCudaAlgorithm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(openCvCudaAlgorithm PUBLIC cxx_std_20)

add_executable(openCvCudaSample main.cpp)
target_link_libraries(openCvCudaSample PRIVATE openCvCudaAlgorithm)

if(BUILD_TESTING)
	include(GoogleTest)
	add_executable(openCvCudaTest cudaThresholdAlgorithmTest.cpp)
	target_link_libraries(openCvCudaTest PRIVATE openCvCudaAlgorithm GTest::gtest_main)
	gtest_discover_tests(openCvCudaTest TEST_PREFIX "opencvCuda."
		DISCOVERY_MODE PRE_TEST PROPERTIES LABELS "opencv-cuda")
	add_test(NAME opencvCuda.selfTest COMMAND openCvCudaSample --self-test)
	set_tests_properties(opencvCuda.selfTest PROPERTIES LABELS "opencv-cuda")
endif()
