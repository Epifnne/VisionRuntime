include(GoogleTest)

foreach(backend IN ITEMS OpenVino TensorRt)
	if(TARGET visionBackend${backend})
		string(TOLOWER "${backend}" backendId)
		add_executable(${backendId}PluginTest backends/vendorPluginTest.cpp)
		target_link_libraries(${backendId}PluginTest PRIVATE visionRuntime GTest::gtest_main)
		target_compile_definitions(${backendId}PluginTest PRIVATE
			TEST_PLUGIN_PATH="$<TARGET_FILE:visionBackend${backend}>"
			TEST_BACKEND_ID="${backendId}"
		)
		if(backend STREQUAL "OpenVino")
			target_compile_definitions(${backendId}PluginTest PRIVATE
				TEST_BACKEND_DEVICE="CPU" TEST_ARTIFACT_KIND="ir"
				TEST_PACKAGE_ENV="VISION_OPENVINO_TEST_PACKAGE"
			)
		else()
			target_compile_definitions(${backendId}PluginTest PRIVATE
				TEST_TENSORRT TEST_BACKEND_DEVICE="0" TEST_ARTIFACT_KIND="engine"
				TEST_PACKAGE_ENV="VISION_TENSORRT_TEST_PACKAGE"
			)
		endif()
		add_dependencies(${backendId}PluginTest visionBackend${backend})
		gtest_discover_tests(${backendId}PluginTest TEST_PREFIX "${backendId}.")
	endif()
endforeach()

add_library(fakeBackendPlugin SHARED
	backends/fakeBackendPlugin.cpp
)
target_compile_definitions(fakeBackendPlugin PRIVATE
	VISION_RUNTIME_BACKEND_PLUGIN_EXPORTS
)
target_include_directories(fakeBackendPlugin PRIVATE
	${PROJECT_SOURCE_DIR}/Runtime/include
)
set_target_properties(fakeBackendPlugin PROPERTIES
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN YES
)

add_library(missingEntryPlugin SHARED
	backends/missingEntryPlugin.cpp
)
target_compile_definitions(missingEntryPlugin PRIVATE
	VISION_RUNTIME_BACKEND_PLUGIN_EXPORTS
)
target_include_directories(missingEntryPlugin PRIVATE
	${PROJECT_SOURCE_DIR}/Runtime/include
)
set_target_properties(missingEntryPlugin PROPERTIES
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN YES
)

add_library(incompatibleBackendPlugin SHARED
	backends/incompatibleBackendPlugin.cpp
)
target_compile_definitions(incompatibleBackendPlugin PRIVATE
	VISION_RUNTIME_BACKEND_PLUGIN_EXPORTS
)
target_include_directories(incompatibleBackendPlugin PRIVATE
	${PROJECT_SOURCE_DIR}/Runtime/include
)
set_target_properties(incompatibleBackendPlugin PROPERTIES
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN YES
)

add_executable(backendPluginLoaderTest
	backends/backendPluginLoaderTest.cpp
)
target_compile_definitions(backendPluginLoaderTest PRIVATE
	VISION_RUNTIME_FAKE_PLUGIN_PATH="$<TARGET_FILE:fakeBackendPlugin>"
	VISION_RUNTIME_MISSING_ENTRY_PLUGIN_PATH="$<TARGET_FILE:missingEntryPlugin>"
	VISION_RUNTIME_INCOMPATIBLE_PLUGIN_PATH="$<TARGET_FILE:incompatibleBackendPlugin>"
)
target_link_libraries(backendPluginLoaderTest PRIVATE
	visionRuntime GTest::gtest_main
)
add_dependencies(backendPluginLoaderTest
	fakeBackendPlugin
	missingEntryPlugin
	incompatibleBackendPlugin
)
gtest_discover_tests(backendPluginLoaderTest)

add_executable(buildProfileTest
	config/buildProfileTest.cpp
)
target_link_libraries(buildProfileTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(buildProfileTest)

add_executable(configLoaderTest
	config/configLoaderTest.cpp
)
target_link_libraries(configLoaderTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(configLoaderTest)

add_executable(modelPackageLoaderTest
	config/modelPackageLoaderTest.cpp
)
target_link_libraries(modelPackageLoaderTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(modelPackageLoaderTest)

add_executable(anomalyPresetTest
	runtime/anomalyPresetTest.cpp
)
target_compile_definitions(anomalyPresetTest PRIVATE
	VISION_RUNTIME_TEST_PLUGIN_DIRECTORY="$<TARGET_FILE_DIR:anomalyPresetTest>/plugins"
)
target_link_libraries(anomalyPresetTest PRIVATE visionRuntime GTest::gtest_main)
add_dependencies(anomalyPresetTest fakeBackendPlugin)
add_custom_command(TARGET anomalyPresetTest POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"$<TARGET_FILE_DIR:anomalyPresetTest>/plugins/fake"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:fakeBackendPlugin>"
		"$<TARGET_FILE_DIR:anomalyPresetTest>/plugins/fake/vision-backend-fake${CMAKE_SHARED_LIBRARY_SUFFIX}"
	VERBATIM
)
gtest_discover_tests(anomalyPresetTest)

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

add_executable(cameraDeviceContractTest
	camera/cameraDeviceContractTest.cpp
)
target_include_directories(cameraDeviceContractTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(cameraDeviceContractTest PRIVATE GTest::gtest_main)
gtest_discover_tests(cameraDeviceContractTest)

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
target_link_libraries(pipelineTest PRIVATE visionRuntime GTest::gtest_main)
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

add_executable(anomalyThresholdPostprocessorTest
	postprocess/anomalyThresholdPostprocessorTest.cpp
)
target_include_directories(anomalyThresholdPostprocessorTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(anomalyThresholdPostprocessorTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(anomalyThresholdPostprocessorTest)

add_executable(pipelineExecutorTest
	executor/pipelineExecutorTest.cpp
)
target_include_directories(pipelineExecutorTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(pipelineExecutorTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(pipelineExecutorTest)

add_executable(batchPipelineExecutorTest
	executor/batchPipelineExecutorTest.cpp
)
target_include_directories(batchPipelineExecutorTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(batchPipelineExecutorTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(batchPipelineExecutorTest)

add_executable(multiCameraSessionTest
	runtime/multiCameraSessionTest.cpp
)
target_include_directories(multiCameraSessionTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(multiCameraSessionTest PRIVATE visionRuntime GTest::gtest_main)
gtest_discover_tests(multiCameraSessionTest)