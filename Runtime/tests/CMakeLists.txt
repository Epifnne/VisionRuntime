include(GoogleTest)

add_executable(buildProfileTest
	config/buildProfileTest.cpp
)
target_link_libraries(buildProfileTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(buildProfileTest)

if(VISION_INFERENCE_PLATFORM STREQUAL "TENSORRT_NVIDIA")
	add_executable(tensorRtBackendTest
		backends/tensorRtBackendTest.cpp
	)
	target_include_directories(tensorRtBackendTest PRIVATE
		${PROJECT_SOURCE_DIR}/Runtime
		${PROJECT_SOURCE_DIR}/Runtime/include)
	target_link_libraries(tensorRtBackendTest PRIVATE
		visionRuntime GTest::gtest_main)
	if(WIN32)
		_vision_deploy_tensorrt_runtime(tensorRtBackendTest)
	endif()
	if(MSVC)
		_vision_deploy_openblas_runtime(tensorRtBackendTest)
	endif()
	gtest_discover_tests(tensorRtBackendTest)
endif()

add_executable(configLoaderTest
	config/configLoaderTest.cpp
)
target_link_libraries(configLoaderTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(configLoaderTest)

foreach(testTarget IN ITEMS typeConstraintTest statusTest resultTest tensorTest)
	add_executable(${testTarget}
		core/${testTarget}.cpp
	)
	target_include_directories(${testTarget} PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
	target_link_libraries(${testTarget} PRIVATE GTest::gtest_main)
	gtest_discover_tests(${testTarget})
endforeach()

add_executable(frameBufferPoolTest
	camera/frameBufferPoolTest.cpp
)
target_include_directories(frameBufferPoolTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(frameBufferPoolTest PRIVATE GTest::gtest_main)
gtest_discover_tests(frameBufferPoolTest)

add_executable(cpuBufferPoolTest
	memory/cpuBufferPoolTest.cpp
)
target_include_directories(cpuBufferPoolTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(cpuBufferPoolTest PRIVATE GTest::gtest_main)
gtest_discover_tests(cpuBufferPoolTest)

add_executable(cameraSourceOptionsTest
	camera/cameraSourceOptionsTest.cpp
)
target_include_directories(cameraSourceOptionsTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(cameraSourceOptionsTest PRIVATE GTest::gtest_main)
gtest_discover_tests(cameraSourceOptionsTest)

add_executable(continuousCameraSourceTest
	camera/continuousCameraSourceTest.cpp
)
target_include_directories(continuousCameraSourceTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(continuousCameraSourceTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(continuousCameraSourceTest)

add_executable(frameSourceFactoryTest
	camera/frameSourceFactoryTest.cpp
)
target_include_directories(frameSourceFactoryTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(frameSourceFactoryTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(frameSourceFactoryTest)

add_executable(timedTriggerSourceTest
	camera/timedTriggerSourceTest.cpp
)
target_include_directories(timedTriggerSourceTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(timedTriggerSourceTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(timedTriggerSourceTest)

add_executable(fileSourceTest
	camera/fileSourceTest.cpp
)
target_include_directories(fileSourceTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(fileSourceTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(fileSourceTest)

add_executable(pipelinePacketTest
	pipeline/pipelinePacketTest.cpp
)
target_include_directories(pipelinePacketTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(pipelinePacketTest PRIVATE GTest::gtest_main)
gtest_discover_tests(pipelinePacketTest)

add_executable(pipelineTest
	pipeline/pipelineTest.cpp
)
target_include_directories(pipelineTest PRIVATE
	${PROJECT_SOURCE_DIR}/Runtime
	${PROJECT_SOURCE_DIR}/Runtime/include
)
target_link_libraries(pipelineTest PRIVATE GTest::gtest_main)
gtest_discover_tests(pipelineTest)

add_executable(openCvPipelineTest
	pipeline/openCvPipelineTest.cpp
)
target_include_directories(openCvPipelineTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(openCvPipelineTest PRIVATE GTest::gtest_main)
gtest_discover_tests(openCvPipelineTest)

add_executable(preprocessChainTest
	preprocess/preprocessChainTest.cpp
)
target_include_directories(preprocessChainTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(preprocessChainTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(preprocessChainTest)

add_executable(anomalyPostprocessorTest
	postprocess/anomalyPostprocessorTest.cpp
)
target_include_directories(anomalyPostprocessorTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(anomalyPostprocessorTest PRIVATE visionRuntime GTest::gtest_main)
if(MSVC)
	_vision_deploy_openblas_runtime(anomalyPostprocessorTest)
endif()
gtest_discover_tests(anomalyPostprocessorTest)

add_executable(pipelineExecutorTest
	executor/pipelineExecutorTest.cpp
)
target_include_directories(pipelineExecutorTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(pipelineExecutorTest PRIVATE GTest::gtest_main)
gtest_discover_tests(pipelineExecutorTest)