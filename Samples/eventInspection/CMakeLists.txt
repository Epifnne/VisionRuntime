if(VISION_PRODUCT_APPLICATION STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(eventApplication visionProductApplication)
	set(eventContract visionProductContract)
else()
	set(eventApplication eventInspectionApplication)
	set(eventContract eventInspectionContract)
endif()
add_library(${eventContract} STATIC businessContract.cpp)
target_link_libraries(${eventContract} PUBLIC visionService)
add_library(${eventApplication} STATIC businessApplication.cpp)
target_link_libraries(${eventApplication} PUBLIC ${eventContract} PRIVATE opencv_core)
target_include_directories(${eventApplication} SYSTEM PRIVATE
	"${PROJECT_SOURCE_DIR}/Thirdparty/opencv/4.12.0/modules/core/include" "${CMAKE_BINARY_DIR}")
set_property(TARGET ${eventApplication} PROPERTY VISION_PRODUCT_ID eventInspection)
add_executable(eventInspectionSample main.cpp)
target_link_libraries(eventInspectionSample PRIVATE ${eventApplication})
target_compile_definitions(eventInspectionSample PRIVATE
	VISION_EVENT_SAMPLE_PROFILE="${CMAKE_CURRENT_SOURCE_DIR}/profile.json")
if(eventApplication STREQUAL "visionProductApplication")
	vision_export_product(${eventApplication} CONTRACT ${eventContract}
		PROFILE "${CMAKE_CURRENT_SOURCE_DIR}/profile.json" CLI eventInspectionSample)
endif()
target_include_directories(eventInspectionSample SYSTEM PRIVATE
	"${PROJECT_SOURCE_DIR}/Thirdparty/opencv/4.12.0/modules/core/include"
	"${CMAKE_BINARY_DIR}")

if(BUILD_TESTING)
	add_test(NAME eventInspectionSample COMMAND eventInspectionSample)
	add_test(NAME eventInspectionMissingView COMMAND eventInspectionSample --missing-view)
endif()

set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
if(MSVC AND NOT MSVC_REDIST_DIR AND IS_DIRECTORY "$ENV{VCToolsRedistDir}")
	set(MSVC_REDIST_DIR "$ENV{VCToolsRedistDir}" CACHE PATH "MSVC redistributable root" FORCE)
endif()
include(InstallRequiredSystemLibraries)
if(MSVC AND NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
	message(FATAL_ERROR "Publishing eventInspection requires the MSVC CRT; set MSVC_REDIST_DIR")
endif()
set(eventSampleOutput "${PROJECT_SOURCE_DIR}/Publish/examples/eventInspection")
if(VISION_DESIGN_KIT_ROOT)
	set(eventSampleOutput "${VISION_DESIGN_KIT_ROOT}/examples/eventInspection")
endif()
add_custom_target(publishEventInspectionSample
	COMMAND ${CMAKE_COMMAND} -E make_directory "${eventSampleOutput}"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"$<TARGET_FILE:eventInspectionSample>"
		"${CMAKE_CURRENT_SOURCE_DIR}/run.cmd"
		"${CMAKE_CURRENT_SOURCE_DIR}/README.md"
		"${CMAKE_CURRENT_SOURCE_DIR}/profile.json"
		${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
		"${eventSampleOutput}"
	DEPENDS eventInspectionSample
	VERBATIM)
