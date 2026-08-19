find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_custom_target(visionModelc
	COMMAND ${Python3_EXECUTABLE}
		${CMAKE_CURRENT_SOURCE_DIR}/vision-modelc.py --help
	VERBATIM
)

add_custom_target(visionRuntimeTools)
add_dependencies(visionRuntimeTools visionModelc)