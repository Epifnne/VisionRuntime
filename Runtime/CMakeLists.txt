add_library(visonRuntime
	src/camera/fileSource.cpp
	src/preprocess/openCvNchwPreprocessor.cpp
	src/preprocess/openCvPreprocessNodes.cpp
	src/preprocess/preprocessChain.cpp
)

target_include_directories(visonRuntime
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
	PRIVATE
		${PROJECT_SOURCE_DIR}/Thirdparty/opencv/4.12.0/modules/core/include
		${PROJECT_SOURCE_DIR}/Thirdparty/opencv/4.12.0/modules/imgcodecs/include
		${CMAKE_BINARY_DIR}
)

target_link_libraries(visonRuntime
	PRIVATE
		opencv_imgcodecs
)