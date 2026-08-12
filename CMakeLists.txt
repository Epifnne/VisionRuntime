cmake_minimum_required(VERSION 3.25)

project(VisonRuntime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

set(BUILD_LIST core,imgcodecs,imgproc CACHE STRING "OpenCV modules required by VisonRuntime" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_java OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_python_bindings_generator OFF CACHE BOOL "" FORCE)
set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
set(WITH_GSTREAMER OFF CACHE BOOL "" FORCE)
set(WITH_IPP OFF CACHE BOOL "" FORCE)
set(WITH_ADE OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/opencv/4.12.0 EXCLUDE_FROM_ALL)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/gtest/1.17.0 EXCLUDE_FROM_ALL)
add_subdirectory(Runtime)
add_subdirectory(Runtime/tests)
