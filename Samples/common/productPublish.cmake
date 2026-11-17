set(VISION_DESIGN_KIT_ROOT "${PROJECT_SOURCE_DIR}/Publish" CACHE PATH "Root containing the replaceable release design kit")
set(VISION_SAMPLE_PUBLISH_ROOT "${VISION_DESIGN_KIT_ROOT}")
if(WIN32)
	set(VISION_SAMPLE_PLATFORM windows)
	get_filename_component(qtBin "${Qt6_DIR}/../../../bin" ABSOLUTE)
	find_program(VISION_SAMPLE_WINDEPLOYQT windeployqt HINTS "${qtBin}" REQUIRED)
else()
	set(VISION_SAMPLE_PLATFORM linux)
	set(VISION_SAMPLE_WINDEPLOYQT "")
endif()
set(VISION_SAMPLE_PUBLISH_DIR "${VISION_SAMPLE_PUBLISH_ROOT}/release")
set(sampleCameraRuntime "")
if(WIN32 AND VISION_CAMERA_SDK STREQUAL "HIK_MVS")
	get_target_property(sampleCameraRuntime VisionHikMvs VISION_HIK_MVS_RUNTIME_DIRECTORY)
endif()
set(productManifest "")
set(productDependencies "")
set(productCliFile "")
set(productAssets "")
set(extraProfiles "")
set(manifestGenerator "")
if(VISION_PRODUCT_APPLICATION)
	file(GLOB extraProfiles CONFIGURE_DEPENDS "${VISION_PRODUCT_APPLICATION}/profile-*.json")
	set(manifestGenerator "$<TARGET_FILE:visionProductManifestGenerator>")
	set(VISION_PUBLISH_PRODUCT "${VISION_APPLICATION_ID}")
	get_target_property(sampleProfile visionProductApplication VISION_PRODUCT_PROFILE)
	get_target_property(productManifest visionProductApplication VISION_PRODUCT_MANIFEST)
	get_target_property(VISION_SAMPLE_BACKEND visionProductApplication VISION_PRODUCT_BACKEND)
	if(NOT VISION_SAMPLE_BACKEND)
		set(VISION_SAMPLE_BACKEND "")
	endif()
	if(NOT sampleProfile OR NOT productManifest)
		message(FATAL_ERROR "Business project must declare its profile and manifest with vision_export_product")
	endif()
	list(APPEND productDependencies visionProductManifest)
	get_target_property(productCli visionProductApplication VISION_PRODUCT_CLI)
	list(APPEND productDependencies "${productCli}")
	set(productCliFile "$<TARGET_FILE:${productCli}>")
	get_target_property(productAssets visionProductApplication VISION_PRODUCT_PROFILE_ASSETS)
	if(NOT productAssets)
		set(productAssets "")
	endif()
else()
	set(VISION_PUBLISH_PRODUCT "${VISION_PRODUCT_SAMPLE}")
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${VISION_SAMPLE_DIRECTORY}/profile.json")
	file(READ "${VISION_SAMPLE_DIRECTORY}/profile.json" sampleDocument)
	string(JSON sampleDocument SET "${sampleDocument}" model backendId "\"${VISION_SAMPLE_BACKEND}\"")
	string(JSON sampleDocument SET "${sampleDocument}" model device "\"${VISION_SAMPLE_DEVICE}\"")
	string(JSON sampleDocument SET "${sampleDocument}" model pluginDirectory
		"\"../runtimeTemplates/${VISION_SAMPLE_PLATFORM}/plugins\"")
	set(sampleProfile "${CMAKE_CURRENT_BINARY_DIR}/sample-profile.json")
	file(WRITE "${sampleProfile}" "${sampleDocument}\n")
	file(GLOB sourceProfiles CONFIGURE_DEPENDS "${VISION_SAMPLE_DIRECTORY}/profile-*.json")
	foreach(sourceProfile IN LISTS sourceProfiles)
		file(READ "${sourceProfile}" document)
		string(JSON document SET "${document}" model backendId "\"${VISION_SAMPLE_BACKEND}\"")
		string(JSON document SET "${document}" model device "\"${VISION_SAMPLE_DEVICE}\"")
		string(JSON document SET "${document}" model pluginDirectory
			"\"../runtimeTemplates/${VISION_SAMPLE_PLATFORM}/plugins\"")
		get_filename_component(name "${sourceProfile}" NAME)
		set(extraProfile "${CMAKE_CURRENT_BINARY_DIR}/${name}")
		set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${sourceProfile}")
		file(WRITE "${extraProfile}" "${document}\n")
		list(APPEND extraProfiles "${extraProfile}")
	endforeach()
	include("${CMAKE_CURRENT_LIST_DIR}/anomalySample.cmake")
	vision_add_anomaly_sample(${VISION_PRODUCT_SAMPLE})
	list(APPEND productDependencies ${VISION_PRODUCT_SAMPLE}Sample)
	set(productCliFile "$<TARGET_FILE:${VISION_PRODUCT_SAMPLE}Sample>")
endif()
add_custom_target(publishSampleShell
	COMMAND ${CMAKE_COMMAND}
		"-DOUTPUT=${VISION_SAMPLE_PUBLISH_DIR}"
		"-DPUBLISH_ROOT=${VISION_SAMPLE_PUBLISH_ROOT}"
		"-DSAMPLE=${VISION_PUBLISH_PRODUCT}"
		"-DSHELL_EXE=$<TARGET_FILE:visionShell>"
		"-DDESIGNER_EXE=$<TARGET_FILE:visionDesigner>"
		"-DPROFILE=${sampleProfile}"
		"-DEXTRA_PROFILES=${extraProfiles}"
		"-DMANIFEST_GENERATOR=${manifestGenerator}"
		"-DMANIFEST=${productManifest}"
		"-DCLI_EXE=${productCliFile}"
		"-DPROFILE_ASSETS=${productAssets}"
		"-DWINDEPLOYQT=${VISION_SAMPLE_WINDEPLOYQT}"
		"-DQML_DIR=${PROJECT_SOURCE_DIR}/Shell"
		"-DDESIGNER_QML_DIR=${PROJECT_SOURCE_DIR}/Tools/uiDesigner"
		"-DBACKEND=${VISION_SAMPLE_BACKEND}"
		"-DCAMERA_RUNTIME=${sampleCameraRuntime}"
		"-DPLATFORM=${VISION_SAMPLE_PLATFORM}"
		-P "${PROJECT_SOURCE_DIR}/Samples/common/publishDesignKit.cmake"
	DEPENDS visionShell visionDesigner ${productDependencies}
	VERBATIM)
