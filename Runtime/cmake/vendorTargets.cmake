function(vision_add_hik_mvs_target)
	if(TARGET Vision::HikMvs)
		return()
	endif()
	set(HIK_MVS_ROOT "" CACHE PATH "Hikrobot MVS SDK root")
	find_path(HIK_MVS_INCLUDE_DIRECTORY MvCameraControl.h
		HINTS "${HIK_MVS_ROOT}"
		PATH_SUFFIXES Development/Includes include
		NO_DEFAULT_PATH
	)
	find_library(HIK_MVS_LIBRARY NAMES MvCameraControl
		HINTS "${HIK_MVS_ROOT}"
		PATH_SUFFIXES Development/Libraries/win64 lib/64 lib
		NO_DEFAULT_PATH
	)
	if(NOT HIK_MVS_INCLUDE_DIRECTORY OR NOT HIK_MVS_LIBRARY)
		message(FATAL_ERROR
			"HIK_MVS profile requires the Hikrobot MVS SDK. Set HIK_MVS_ROOT to its root directory.")
	endif()
	add_library(VisionHikMvs UNKNOWN IMPORTED)
	set_target_properties(VisionHikMvs PROPERTIES
		IMPORTED_LOCATION "${HIK_MVS_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${HIK_MVS_INCLUDE_DIRECTORY}"
	)
	add_library(Vision::HikMvs ALIAS VisionHikMvs)
endfunction()

function(vision_add_openvino_target)
	if(TARGET Vision::OpenVino)
		return()
	endif()
	find_package(OpenVINO CONFIG REQUIRED COMPONENTS Runtime)
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
	set(TENSORRT_ROOT "" CACHE PATH "TensorRT root")
	find_path(TENSORRT_INCLUDE_DIRECTORY NvInfer.h
		HINTS "${TENSORRT_ROOT}" PATH_SUFFIXES include NO_DEFAULT_PATH)
	find_library(TENSORRT_LIBRARY NAMES nvinfer
		HINTS "${TENSORRT_ROOT}" PATH_SUFFIXES lib lib/x64 NO_DEFAULT_PATH)
	if(NOT TENSORRT_INCLUDE_DIRECTORY OR NOT TENSORRT_LIBRARY)
		message(FATAL_ERROR "Set TENSORRT_ROOT to a complete TensorRT distribution")
	endif()
	add_library(VisionTensorRt UNKNOWN IMPORTED)
	set_target_properties(VisionTensorRt PROPERTIES
		IMPORTED_LOCATION "${TENSORRT_LIBRARY}"
		INTERFACE_INCLUDE_DIRECTORIES "${TENSORRT_INCLUDE_DIRECTORY}"
	)
	add_library(Vision::TensorRt ALIAS VisionTensorRt)
endfunction()