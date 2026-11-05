include_guard(GLOBAL)

if(MSVC)
	set(OpenCV_ARCH x64)
	set(OpenCV_RUNTIME vc17)
endif()

set(visionOpenCvModules core imgcodecs imgproc)
set(WITH_CUDA ${VISION_WITH_OPENCV_CUDA} CACHE BOOL "" FORCE)
if(VISION_WITH_OPENCV_CUDA)
	if(WIN32 AND NOT MSVC)
		message(FATAL_ERROR "OpenCV CUDA on Windows requires MSVC (not MinGW)")
	endif()
	if(NOT VISION_OPENCV_CUDA_ARCH_BIN)
		message(FATAL_ERROR
			"Set VISION_OPENCV_CUDA_ARCH_BIN to the deployed GPU compute capability, e.g. 8.6")
	endif()
	set(visionOpenCvCudaModules cudev cudaarithm cudaimgproc cudafilters cudawarping)
	foreach(module IN LISTS visionOpenCvCudaModules)
		if(NOT EXISTS "${VISION_OPENCV_CONTRIB_ROOT}/modules/${module}/CMakeLists.txt")
			message(FATAL_ERROR
				"OpenCV contrib 4.14.0 is required at VISION_OPENCV_CONTRIB_ROOT. "
				"Configure once with VISION_WITH_OPENCV_CUDA=OFF and build "
				"bootstrapOpenCvCudaDependencies, then enable CUDA.")
		endif()
	endforeach()
	find_package(CUDAToolkit REQUIRED)
	if(MSVC)
		# Recent CUDA CCCL headers require the conforming host preprocessor.
		string(APPEND CMAKE_CXX_FLAGS " /Zc:preprocessor")
	endif()
	set(CUDA_TOOLKIT_ROOT_DIR "${CUDAToolkit_LIBRARY_ROOT}" CACHE PATH "" FORCE)
	set(CUDA_NVCC_EXECUTABLE "${CUDAToolkit_NVCC_EXECUTABLE}" CACHE FILEPATH "" FORCE)
	set(visionOpenCvExtraModules "")
	foreach(module IN LISTS visionOpenCvCudaModules)
		list(APPEND visionOpenCvExtraModules "${VISION_OPENCV_CONTRIB_ROOT}/modules/${module}")
	endforeach()
	set(OPENCV_EXTRA_MODULES_PATH "${visionOpenCvExtraModules}" CACHE STRING "" FORCE)
	set(CUDA_ARCH_BIN "${VISION_OPENCV_CUDA_ARCH_BIN}" CACHE STRING "" FORCE)
	set(CUDA_ARCH_PTX "" CACHE STRING "" FORCE)
	list(APPEND visionOpenCvModules ${visionOpenCvCudaModules})
endif()
list(JOIN visionOpenCvModules "," visionOpenCvBuildList)
set(BUILD_LIST "${visionOpenCvBuildList}" CACHE STRING "OpenCV modules required by VisionRuntime" FORCE)
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
add_subdirectory(Thirdparty/opencv/4.14.0 EXCLUDE_FROM_ALL)

if(VISION_WITH_OPENCV_CUDA)
	add_library(visionOpenCvCuda INTERFACE)
	add_library(Vision::OpenCvCuda ALIAS visionOpenCvCuda)
	foreach(module IN LISTS visionOpenCvCudaModules)
		if(NOT TARGET opencv_${module})
			message(FATAL_ERROR
				"OpenCV CUDA module ${module} was not built; inspect CUDA detection above. "
				"CPU fallback is not supported.")
		endif()
		target_link_libraries(visionOpenCvCuda INTERFACE opencv_${module})
		target_include_directories(visionOpenCvCuda SYSTEM INTERFACE
			"${VISION_OPENCV_CONTRIB_ROOT}/modules/${module}/include")
	endforeach()
	target_include_directories(visionOpenCvCuda SYSTEM INTERFACE
		"${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/opencv/4.14.0/modules/core/include"
		"${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/opencv/4.14.0/modules/imgproc/include"
		"${CMAKE_BINARY_DIR}")
endif()

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