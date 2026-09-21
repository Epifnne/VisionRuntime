include(GoogleTest)

add_executable(endpointRegistryTest
	endpoints/endpointRegistryTest.cpp
)
target_link_libraries(endpointRegistryTest PRIVATE visionService GTest::gtest_main)
gtest_discover_tests(endpointRegistryTest)

add_executable(profileLoaderTest
	profile/profileLoaderTest.cpp
)
target_link_libraries(profileLoaderTest PRIVATE visionService GTest::gtest_main)
gtest_discover_tests(profileLoaderTest)

add_executable(sessionControllerTest
	session/sessionControllerTest.cpp
)
target_link_libraries(sessionControllerTest PRIVATE visionService GTest::gtest_main)
gtest_discover_tests(sessionControllerTest)

add_executable(visionServiceDirectorySessionTest
	service/visionServiceDirectorySessionTest.cpp
)
target_compile_definitions(visionServiceDirectorySessionTest PRIVATE
	VISION_SERVICE_TEST_PLUGIN_DIRECTORY="$<TARGET_FILE_DIR:visionServiceDirectorySessionTest>/plugins"
)
target_link_libraries(visionServiceDirectorySessionTest PRIVATE visionService GTest::gtest_main)
add_dependencies(visionServiceDirectorySessionTest fakeBackendPlugin)
add_custom_command(TARGET visionServiceDirectorySessionTest POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory
		"$<TARGET_FILE_DIR:visionServiceDirectorySessionTest>/plugins/fake"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:fakeBackendPlugin>"
		"$<TARGET_FILE_DIR:visionServiceDirectorySessionTest>/plugins/fake/vision-backend-fake${CMAKE_SHARED_LIBRARY_SUFFIX}"
	VERBATIM
)
gtest_discover_tests(visionServiceDirectorySessionTest)
