include(GoogleTest)

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

add_executable(pipelinePacketTest
	pipeline/pipelinePacketTest.cpp
)
target_include_directories(pipelinePacketTest PRIVATE ${PROJECT_SOURCE_DIR}/Runtime/include)
target_link_libraries(pipelinePacketTest PRIVATE GTest::gtest_main)
gtest_discover_tests(pipelinePacketTest)