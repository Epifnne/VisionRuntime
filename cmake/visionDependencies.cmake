include_guard(GLOBAL)

if(MSVC)
	set(OpenCV_ARCH x64)
	set(OpenCV_RUNTIME vc17)
endif()

set(BUILD_LIST core,imgcodecs,imgproc CACHE STRING "OpenCV modules required by VisionRuntime" FORCE)
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
# Static OpenCV defaults to a static CRT (BUILD_WITH_STATIC_CRT=ON); the
# superbuild mandates dynamic CRT to match prebuilt Qt (see root CMakeLists).
set(BUILD_WITH_STATIC_CRT OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/opencv/4.12.0 EXCLUDE_FROM_ALL)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
# Keep gtest on the same dynamic CRT as the rest of the superbuild.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/gtest/1.17.0 EXCLUDE_FROM_ALL)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/json/3.12.0 EXCLUDE_FROM_ALL)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/spdlog/1.15.3 EXCLUDE_FROM_ALL)