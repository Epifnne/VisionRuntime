set(VISION_SAMPLE_PUBLISH_ROOT "${PROJECT_SOURCE_DIR}/Publish")
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
list(LENGTH VISION_SAMPLE_SOURCE_IDS sampleSourceCount)
set(sampleViews "")
set(sampleEndpoints "{\"name\":\"session.start\",\"kind\":\"command\"},{\"name\":\"session.stop\",\"kind\":\"command\"},{\"name\":\"session.recover\",\"kind\":\"command\",\"access\":\"engineer\"},{\"name\":\"session.summary\",\"kind\":\"state\"}")
foreach(sourceId IN LISTS VISION_SAMPLE_SOURCE_IDS)
	if(sampleViews)
		string(APPEND sampleViews ",")
	endif()
	string(APPEND sampleViews "{\"type\":\"imageView\",\"endpoint\":\"stream.${sourceId}\",\"fillWidth\":true,\"fillHeight\":true}")
	string(APPEND sampleEndpoints ",{\"name\":\"stream.${sourceId}\",\"kind\":\"stream\"}")
endforeach()
set(sampleProfile "${CMAKE_CURRENT_BINARY_DIR}/sample-profile.json")
file(WRITE "${sampleProfile}" "{
  \"schemaVersion\":{\"major\":1,\"minor\":0},
  \"id\":\"${VISION_PRODUCT_SAMPLE}\",\"name\":\"${VISION_PRODUCT_SAMPLE}\",
  \"runtimeProduct\":\"${VISION_PRODUCT_SAMPLE}\",
  \"endpoints\":[${sampleEndpoints}],
  \"sources\":[${VISION_SAMPLE_SOURCES}],
  \"model\":{\"packagePath\":\"model\",\"backendId\":\"${VISION_SAMPLE_BACKEND}\",\"device\":\"${VISION_SAMPLE_DEVICE}\",\"pluginDirectory\":\"../runtimeTemplates/${VISION_SAMPLE_PLATFORM}/plugins\"},
  \"backend\":{\"streams\":2,\"dynamicBatch\":true},
  \"pipeline\":{\"resizeShortSide\":${VISION_SAMPLE_RESIZE},\"cropWidth\":224,\"cropHeight\":224,\"mean\":[0.449],\"standardDeviation\":[0.226],\"threshold\":2.0,\"maxBatchSize\":${sampleSourceCount},\"flushTimeoutMilliseconds\":20,\"queueCapacity\":16,\"stageQueueCapacity\":2,\"queueFullPolicy\":\"block\"},
  \"ui\":{\"pages\":[{\"id\":\"monitor\",\"title\":\"${VISION_PRODUCT_SAMPLE}\",\"layout\":\"column\",\"children\":[
    {\"type\":\"row\",\"fillWidth\":true,\"fillHeight\":true,\"children\":[${sampleViews}]},
    {\"type\":\"row\",\"children\":[{\"type\":\"commandButton\",\"endpoint\":\"session.start\",\"label\":\"Start\"},{\"type\":\"commandButton\",\"endpoint\":\"session.stop\",\"label\":\"Stop\"},{\"type\":\"commandButton\",\"endpoint\":\"session.recover\",\"label\":\"Recover\"},{\"type\":\"stateCard\",\"endpoint\":\"session.summary\",\"title\":\"Summary\"}]}
  ]}]}
}\n")
add_custom_target(publishSampleShell
	COMMAND ${CMAKE_COMMAND}
		"-DOUTPUT=${VISION_SAMPLE_PUBLISH_DIR}"
		"-DPUBLISH_ROOT=${VISION_SAMPLE_PUBLISH_ROOT}"
		"-DSAMPLE=${VISION_PRODUCT_SAMPLE}"
		"-DSHELL_EXE=$<TARGET_FILE:visionShell>"
		"-DDESIGNER_EXE=$<TARGET_FILE:visionDesigner>"
		"-DPROFILE=${sampleProfile}"
		"-DWINDEPLOYQT=${VISION_SAMPLE_WINDEPLOYQT}"
		"-DQML_DIR=${PROJECT_SOURCE_DIR}/Shell"
		"-DDESIGNER_QML_DIR=${PROJECT_SOURCE_DIR}/Tools/uiDesigner"
		"-DBACKEND=${VISION_SAMPLE_BACKEND}"
		"-DCAMERA_RUNTIME=${sampleCameraRuntime}"
		"-DPLATFORM=${VISION_SAMPLE_PLATFORM}"
		-P "${PROJECT_SOURCE_DIR}/Samples/common/publishDesignKit.cmake"
	DEPENDS visionShell visionDesigner
	VERBATIM)
