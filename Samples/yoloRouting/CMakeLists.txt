cmake_minimum_required(VERSION 3.25)

if(NOT TARGET visionRuntime)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	project(YoloRoutingSample LANGUAGES CXX)
	include(CTest)
	set(VISION_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.."
		CACHE PATH "Path to the VisionRuntime source package")
	add_subdirectory("${VISION_RUNTIME_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/VisionRuntime")
endif()

add_library(yoloRoutingAlgorithm STATIC routingAlgorithm.cpp)
target_link_libraries(yoloRoutingAlgorithm PUBLIC visionRuntime opencv_imgproc)
target_include_directories(yoloRoutingAlgorithm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(yoloRoutingAlgorithm SYSTEM PUBLIC
	"${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/opencv/4.12.0/modules/core/include"
	"${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/opencv/4.12.0/modules/imgproc/include"
	"${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/opencv/4.12.0/modules/imgcodecs/include"
	"${CMAKE_BINARY_DIR}"
)
target_compile_features(yoloRoutingAlgorithm PUBLIC cxx_std_20)

add_executable(yoloRoutingSample yoloRoutingSample.cpp)
target_link_libraries(yoloRoutingSample PRIVATE yoloRoutingAlgorithm opencv_imgcodecs)

if(BUILD_TESTING)
	include(GoogleTest)
	add_executable(yoloRoutingTest routingAlgorithmTest.cpp)
	target_link_libraries(yoloRoutingTest PRIVATE yoloRoutingAlgorithm GTest::gtest_main)
	gtest_discover_tests(yoloRoutingTest TEST_PREFIX "yoloRouting.")
endif()
