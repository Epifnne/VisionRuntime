cmake_minimum_required(VERSION 3.25)

if(NOT TARGET visionRuntime)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	project(YoloRoutingSample LANGUAGES CXX)
	include(CTest)
	set(VISION_BUILD_SERVICE ON CACHE BOOL "" FORCE)
	set(VISION_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.."
		CACHE PATH "Path to the VisionRuntime source package")
	add_subdirectory("${VISION_RUNTIME_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/VisionRuntime")
endif()

add_library(yoloRoutingAlgorithm STATIC routingAlgorithm.cpp)
target_link_libraries(yoloRoutingAlgorithm PUBLIC visionRuntime opencv_imgproc)
target_include_directories(yoloRoutingAlgorithm PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(yoloRoutingAlgorithm SYSTEM PUBLIC
	"${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/opencv/4.14.0/modules/core/include"
	"${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/opencv/4.14.0/modules/imgproc/include"
	"${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/opencv/4.14.0/modules/imgcodecs/include"
	"${CMAKE_BINARY_DIR}"
)
target_compile_features(yoloRoutingAlgorithm PUBLIC cxx_std_20)

add_executable(yoloRoutingSample yoloRoutingSample.cpp)
if(VISION_PRODUCT_APPLICATION STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(yoloApplication visionProductApplication)
	set(yoloContract visionProductContract)
else()
	set(yoloApplication yoloRoutingApplication)
	set(yoloContract yoloRoutingContract)
endif()
add_library(${yoloContract} STATIC businessContract.cpp)
target_link_libraries(${yoloContract} PUBLIC visionService)
add_library(${yoloApplication} STATIC businessApplication.cpp)
target_link_libraries(${yoloApplication} PUBLIC ${yoloContract} PRIVATE yoloRoutingAlgorithm opencv_imgcodecs)
target_link_libraries(yoloRoutingSample PRIVATE ${yoloApplication})
set_property(TARGET ${yoloApplication} PROPERTY VISION_PRODUCT_ID yoloRouting)
set_property(TARGET ${yoloApplication} PROPERTY VISION_PRODUCT_PROFILE_ASSETS
	"${CMAKE_CURRENT_SOURCE_DIR}/../businessExports/images")
if(yoloApplication STREQUAL "visionProductApplication")
	vision_export_product(${yoloApplication} CONTRACT ${yoloContract}
		PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/profile.json" CLI yoloRoutingSample)
endif()

if(BUILD_TESTING)
	include(GoogleTest)
	add_executable(yoloRoutingTest routingAlgorithmTest.cpp)
	target_link_libraries(yoloRoutingTest PRIVATE yoloRoutingAlgorithm GTest::gtest_main)
	gtest_discover_tests(yoloRoutingTest TEST_PREFIX "yoloRouting.")
endif()
