function(vision_add_hik_mvs_target)
	if(TARGET Vision::HikMvs)
		return()
	endif()
	if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
		message(FATAL_ERROR "Hikrobot MVS profile currently requires a 64-bit target")
	endif()
	if(WIN32)
		set(hikMvsPlatformDirectory windows-x86_64)
	elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
		set(hikMvsPlatformDirectory linux-x86_64)
	else()
		message(FATAL_ERROR
			"Hikrobot MVS 4.8.1 is unavailable for "
			"${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
	endif()
	set(defaultHikMvsRoot
		"${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/hik-mvs/4.8.1")
	set(HIK_MVS_ROOT "${defaultHikMvsRoot}" CACHE PATH
		"Hikrobot MVS SDK root")
	find_path(HIK_MVS_INCLUDE_DIRECTORY MvCameraControl.h
		HINTS "${HIK_MVS_ROOT}"
		PATH_SUFFIXES "${hikMvsPlatformDirectory}/include" include Development/Includes
		NO_DEFAULT_PATH
	)
	find_library(HIK_MVS_LIBRARY NAMES MvCameraControl
		HINTS "${HIK_MVS_ROOT}"
		PATH_SUFFIXES "${hikMvsPlatformDirectory}/lib" Development/Libraries/win64 lib/64 lib
		NO_DEFAULT_PATH
	)
	set(hikMvsRuntimeDirectory "")
	set(hikMvsRuntimeMissing FALSE)
	if(WIN32)
		set(hikMvsRuntimeDirectory
			"${HIK_MVS_ROOT}/${hikMvsPlatformDirectory}/bin")
		if(NOT EXISTS "${hikMvsRuntimeDirectory}/MvCameraControl.dll")
			set(hikMvsRuntimeMissing TRUE)
		endif()
	endif()
	if(NOT HIK_MVS_INCLUDE_DIRECTORY OR NOT HIK_MVS_LIBRARY OR
		hikMvsRuntimeMissing)
		message(FATAL_ERROR
			"HIK_MVS profile requires Hikrobot MVS 4.8.1 development files for "
			"${hikMvsPlatformDirectory}. Add include/MvCameraControl.h and the platform "
			"import/shared library and runtime under '${HIK_MVS_ROOT}', or set HIK_MVS_ROOT.")
	endif()
	add_library(VisionHikMvs UNKNOWN IMPORTED)
	set_target_properties(VisionHikMvs PROPERTIES
		IMPORTED_LOCATION "${HIK_MVS_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${HIK_MVS_INCLUDE_DIRECTORY}"
		VISION_HIK_MVS_RUNTIME_DIRECTORY "${hikMvsRuntimeDirectory}"
	)
	add_library(Vision::HikMvs ALIAS VisionHikMvs)
endfunction()

function(vision_add_openvino_target)
	if(TARGET Vision::OpenVino)
		return()
	endif()
	if(NOT OpenVINO_DIR)
		if(WIN32)
			set(openVinoPlatformDirectory windows-x86_64)
		elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
			set(openVinoPlatformDirectory linux-x86_64)
		else()
			message(FATAL_ERROR
				"Set OpenVINO_DIR for the current platform: "
				"${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}")
		endif()
		set(openVinoPackageRoot
			"${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/openvino/2026.2.1/${openVinoPlatformDirectory}")
		find_path(OpenVINO_DIR OpenVINOConfig.cmake
			HINTS "${openVinoPackageRoot}"
			PATH_SUFFIXES cmake openvino/cmake runtime/cmake
			NO_DEFAULT_PATH
		)
		if(NOT OpenVINO_DIR)
			message(FATAL_ERROR
				"OpenVINO 2026.2.1 was not found under '${openVinoPackageRoot}'. "
				"Install the platform package there or set OpenVINO_DIR explicitly.")
		endif()
	endif()
	find_package(OpenVINO CONFIG REQUIRED COMPONENTS Runtime)
	if(WIN32)
		get_target_property(openVinoRuntimeLocation
			openvino::runtime::c IMPORTED_LOCATION_RELEASE)
		if(NOT openVinoRuntimeLocation)
			get_target_property(openVinoRuntimeLocation
				openvino::runtime::c IMPORTED_LOCATION)
		endif()
		if(NOT openVinoRuntimeLocation)
			message(FATAL_ERROR
				"OpenVINO C Runtime target does not expose an imported DLL location")
		endif()
		get_filename_component(openVinoRuntimeDirectory
			"${openVinoRuntimeLocation}" DIRECTORY)
		set(VISION_OPENVINO_RUNTIME_DIRECTORY "${openVinoRuntimeDirectory}"
			CACHE INTERNAL "OpenVINO runtime deployment directory" FORCE)
	endif()

	add_library(VisionOpenVino INTERFACE IMPORTED GLOBAL)
	set_property(TARGET VisionOpenVino PROPERTY
		INTERFACE_LINK_LIBRARIES openvino::runtime::c)
	add_library(Vision::OpenVino ALIAS VisionOpenVino)
endfunction()

function(vision_add_onnx_runtime_target)
	if(TARGET Vision::OnnxRuntime)
		return()
	endif()
	set(ONNX_RUNTIME_ROOT "" CACHE PATH "ONNX Runtime root")
	find_path(ONNX_RUNTIME_INCLUDE_DIRECTORY onnxruntime_cxx_api.h
		HINTS "${ONNX_RUNTIME_ROOT}" PATH_SUFFIXES include NO_DEFAULT_PATH)
	find_library(ONNX_RUNTIME_LIBRARY NAMES onnxruntime
		HINTS "${ONNX_RUNTIME_ROOT}" PATH_SUFFIXES lib NO_DEFAULT_PATH)
	if(NOT ONNX_RUNTIME_INCLUDE_DIRECTORY OR NOT ONNX_RUNTIME_LIBRARY)
		message(FATAL_ERROR "Set ONNX_RUNTIME_ROOT to a complete ONNX Runtime distribution")
	endif()
	add_library(VisionOnnxRuntime UNKNOWN IMPORTED)
	set_target_properties(VisionOnnxRuntime PROPERTIES
		IMPORTED_LOCATION "${ONNX_RUNTIME_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${ONNX_RUNTIME_INCLUDE_DIRECTORY}"
	)
	add_library(Vision::OnnxRuntime ALIAS VisionOnnxRuntime)
endfunction()

function(vision_add_tensorrt_target)
	if(TARGET Vision::TensorRt)
		return()
	endif()
	if(WIN32 AND NOT MSVC)
		message(FATAL_ERROR "TensorRT NVIDIA profile on Windows requires MSVC")
	endif()
	set(defaultTensorRtRoot "")
	if(WIN32)
		set(defaultTensorRtRoot
			"${VISION_RUNTIME_REPOSITORY_DIRECTORY}/Thirdparty/tensorrt/10.16.1.11/windows-x86_64/TensorRT-10.16.1.11")
	endif()
	set(TENSORRT_ROOT "${defaultTensorRtRoot}" CACHE PATH "TensorRT root")
	find_path(TENSORRT_INCLUDE_DIRECTORY NvInfer.h
		HINTS "${TENSORRT_ROOT}" PATH_SUFFIXES include NO_DEFAULT_PATH)
	find_library(TENSORRT_LIBRARY NAMES nvinfer nvinfer_10
		HINTS "${TENSORRT_ROOT}" PATH_SUFFIXES lib lib/x64 NO_DEFAULT_PATH)
	find_library(TENSORRT_PLUGIN_LIBRARY NAMES nvinfer_plugin nvinfer_plugin_10
		HINTS "${TENSORRT_ROOT}" PATH_SUFFIXES lib lib/x64 NO_DEFAULT_PATH)
	find_package(CUDAToolkit REQUIRED)
	if(NOT TENSORRT_INCLUDE_DIRECTORY OR NOT TENSORRT_LIBRARY OR
		NOT TENSORRT_PLUGIN_LIBRARY)
		message(FATAL_ERROR "Set TENSORRT_ROOT to a complete TensorRT distribution")
	endif()
	add_library(VisionTensorRt INTERFACE IMPORTED GLOBAL)
	set_target_properties(VisionTensorRt PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${TENSORRT_INCLUDE_DIRECTORY}"
		INTERFACE_LINK_LIBRARIES "${TENSORRT_LIBRARY};${TENSORRT_PLUGIN_LIBRARY};CUDA::cudart"
	)
	if(WIN32)
		find_file(TENSORRT_RUNTIME_FILE NAMES nvinfer_10.dll
			HINTS "${TENSORRT_ROOT}" PATH_SUFFIXES bin lib NO_DEFAULT_PATH)
		find_file(TENSORRT_PLUGIN_RUNTIME_FILE NAMES nvinfer_plugin_10.dll
			HINTS "${TENSORRT_ROOT}" PATH_SUFFIXES bin lib NO_DEFAULT_PATH)
		file(GLOB cudaRuntimeFiles
			"${CUDAToolkit_BIN_DIR}/cudart64_*.dll"
			"${CUDAToolkit_BIN_DIR}/x64/cudart64_*.dll")
		list(LENGTH cudaRuntimeFiles cudaRuntimeFileCount)
		if(NOT TENSORRT_RUNTIME_FILE OR NOT TENSORRT_PLUGIN_RUNTIME_FILE OR
			NOT cudaRuntimeFileCount EQUAL 1)
			message(FATAL_ERROR
				"TensorRT NVIDIA profile requires nvinfer, plugin, and CUDA runtime DLLs")
		endif()
		list(GET cudaRuntimeFiles 0 cudaRuntimeFile)
		set_property(TARGET VisionTensorRt PROPERTY
			VISION_TENSORRT_RUNTIME_FILES
			"${TENSORRT_RUNTIME_FILE};${TENSORRT_PLUGIN_RUNTIME_FILE};${cudaRuntimeFile}")
	endif()
	add_library(Vision::TensorRt ALIAS VisionTensorRt)
endfunction()