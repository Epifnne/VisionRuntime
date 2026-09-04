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
add_subdirectory(Thirdparty/opencv/4.12.0 EXCLUDE_FROM_ALL)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/gtest/1.17.0 EXCLUDE_FROM_ALL)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/json/3.12.0 EXCLUDE_FROM_ALL)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(Thirdparty/spdlog/1.15.3 EXCLUDE_FROM_ALL)

include(ExternalProject)
if(WIN32)
	if(MINGW)
		get_filename_component(openBlasToolchainDirectory
			"${CMAKE_CXX_COMPILER}" DIRECTORY)
	else()
		set(OPENBLAS_TOOLCHAIN_DIRECTORY "D:/Qt/Tools/mingw1310_64/bin"
			CACHE PATH "MinGW toolchain directory used to build OpenBLAS on Windows")
		set(openBlasToolchainDirectory "${OPENBLAS_TOOLCHAIN_DIRECTORY}")
	endif()
	set(openBlasMake "${openBlasToolchainDirectory}/mingw32-make.exe")
	if(NOT EXISTS "${openBlasMake}")
		message(FATAL_ERROR
			"OpenBLAS MinGW make tool was not found: ${openBlasMake}")
	endif()
	set(openBlasBuildCommand ${CMAKE_COMMAND}
		"-DOPENBLAS_MAKE=${openBlasMake}"
		"-DOPENBLAS_BUILD_DIRECTORY=<BINARY_DIR>"
		"-DOPENBLAS_COMPILER_DIRECTORY=${openBlasToolchainDirectory}"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/buildOpenBlas.cmake")
	if(MSVC)
		list(INSERT openBlasBuildCommand 4
			"-DOPENBLAS_IMPORT_LIBRARY_TOOL=${CMAKE_AR}")
	endif()
else()
	find_program(openBlasMake make REQUIRED)
	set(openBlasBuildCommand "${openBlasMake}" -C "<BINARY_DIR>")
endif()
set(openBlasSourceDirectory "${CMAKE_CURRENT_SOURCE_DIR}/Thirdparty/openblas/0.3.30")
if(MSVC)
	set(openBlasBuildDirectory
		"${CMAKE_CURRENT_BINARY_DIR}/Thirdparty/openblas/0.3.30-single-lapack")
	set(openBlasLibrary "${openBlasBuildDirectory}/libopenblas-msvc.lib")
	set(openBlasRuntime "${openBlasBuildDirectory}/libopenblas.dll")
else()
	set(openBlasBuildDirectory "${CMAKE_CURRENT_BINARY_DIR}/Thirdparty/openblas/0.3.30-lapack")
	set(openBlasLibrary "${openBlasBuildDirectory}/libopenblas.a")
endif()
ExternalProject_Add(openblas_external
	SOURCE_DIR "${openBlasSourceDirectory}"
	BINARY_DIR "${openBlasBuildDirectory}"
	DOWNLOAD_COMMAND ""
	CONFIGURE_COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${openBlasSourceDirectory}" "${openBlasBuildDirectory}"
	BUILD_COMMAND ${openBlasBuildCommand}
	INSTALL_COMMAND ""
	BUILD_BYPRODUCTS "${openBlasLibrary}" ${openBlasRuntime}
)
if(MSVC)
	add_library(openblas SHARED IMPORTED GLOBAL)
	set(openBlasRuntimeFiles "${openBlasRuntime}")
	foreach(runtimeName IN ITEMS
		libgfortran-5.dll
		libgcc_s_seh-1.dll
		libquadmath-0.dll
		libwinpthread-1.dll)
		set(runtimeFile "${openBlasToolchainDirectory}/${runtimeName}")
		if(NOT EXISTS "${runtimeFile}")
			message(FATAL_ERROR
				"Required OpenBLAS runtime file was not found: ${runtimeFile}")
		endif()
		list(APPEND openBlasRuntimeFiles "${runtimeFile}")
	endforeach()
	set_target_properties(openblas PROPERTIES
		IMPORTED_IMPLIB "${openBlasLibrary}"
		IMPORTED_LOCATION "${openBlasRuntime}"
		VISION_OPENBLAS_RUNTIME_FILES "${openBlasRuntimeFiles}"
	)
else()
	add_library(openblas STATIC IMPORTED GLOBAL)
	set_target_properties(openblas PROPERTIES IMPORTED_LOCATION "${openBlasLibrary}")
endif()
if(MINGW OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set_property(TARGET openblas PROPERTY
		INTERFACE_LINK_LIBRARIES "gfortran;quadmath")
endif()
add_dependencies(openblas openblas_external)

set(FAISS_ENABLE_GPU OFF CACHE BOOL "" FORCE)
set(FAISS_ENABLE_MKL OFF CACHE BOOL "" FORCE)
set(FAISS_ENABLE_PYTHON OFF CACHE BOOL "" FORCE)
set(FAISS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(FAISS_ENABLE_EXTRAS OFF CACHE BOOL "" FORCE)
set(FAISS_OPT_LEVEL generic CACHE STRING "" FORCE)
set(BLAS_FOUND TRUE CACHE BOOL "" FORCE)
set(BLAS_LIBRARIES openblas CACHE STRING "" FORCE)
set(LAPACK_FOUND TRUE CACHE BOOL "" FORCE)
set(LAPACK_LIBRARIES openblas CACHE STRING "" FORCE)
set(visionRuntimeBuildTesting "${BUILD_TESTING}")
set(BUILD_TESTING OFF)
add_subdirectory(Thirdparty/faiss/1.12.0 EXCLUDE_FROM_ALL)
set(BUILD_TESTING "${visionRuntimeBuildTesting}")
add_dependencies(faiss openblas_external)
if(MINGW)
	target_compile_options(faiss PRIVATE -fpermissive)
endif()
if(MINGW OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
	target_compile_options(faiss PRIVATE -ffunction-sections -fdata-sections)
	target_link_options(faiss INTERFACE -Wl,--gc-sections)
endif()