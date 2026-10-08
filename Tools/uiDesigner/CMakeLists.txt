# visionDesigner: profile/UI composition tool. Pure Qt Quick; it must not
# link Runtime/Service so it runs on machines without camera SDKs or
# inference stacks. The Shell control set is referenced by file path at
# runtime (development) per Docs/guiShellPlan.md section 6.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_add_executable(visionDesigner
	src/main.cpp
	src/designerBackend.cpp
	src/previewClient.cpp
	src/previewImageProvider.cpp
	include/designerBackend.hpp
	include/previewClient.hpp
	include/previewImageProvider.hpp
)

# NO_CACHEGEN: the designer edits/renders the control tree entirely through
# dynamic delegates (path-string models, Loader recursion). qmlcachegen's
# compiled delegates lose Repeater context properties (index) and break
# required-property array models here; the Shell hit the same class of issue
# with static recursion. This tool is not performance-critical.
qt_add_qml_module(visionDesigner
	URI VisionDesigner
	VERSION 1.0
	NO_CACHEGEN
	QML_FILES
		qml/main.qml
		qml/PalettePane.qml
		qml/CanvasPane.qml
		qml/EditorNode.qml
		qml/PropertyPanel.qml
		qml/ProductPanel.qml
		qml/PreviewPane.qml
)

set_source_files_properties(
	templates/emptyProduct.json
	PROPERTIES QT_RESOURCE_ALIAS "emptyProduct.json")
set_source_files_properties(
	templates/twoCameraDirectory.json
	PROPERTIES QT_RESOURCE_ALIAS "twoCameraDirectory.json")
set_source_files_properties(
	templates/singleCameraDebug.json
	PROPERTIES QT_RESOURCE_ALIAS "singleCameraDebug.json")
qt_add_resources(visionDesigner "templates"
	PREFIX "/templates"
	FILES
		templates/emptyProduct.json
		templates/twoCameraDirectory.json
		templates/singleCameraDebug.json
)

target_include_directories(visionDesigner PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${PROJECT_SOURCE_DIR}/Tools/packageSupport
)

target_link_libraries(visionDesigner PRIVATE
	Qt6::Quick
)
if(PROJECT_IS_TOP_LEVEL AND BUILD_TESTING)
	add_test(NAME visionDesignerSelftest COMMAND visionDesigner --selftest)
	set_tests_properties(visionDesignerSelftest PROPERTIES
		ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
		TIMEOUT 60
	)
endif()

# Publish target: distributable designer package. Windows gets a
# windeployqt self-contained root; Linux ships the exe only (target machine
# provides the Qt runtime, per Docs/clickToRunPackagePlan.md section 5).
# The Shell control set is copied next to the exe so the shipped designer
# can still resolve its preview controls by path (detectControlsPath walks
# up from the executable and finds <root>/Shell/controls).
set(DESIGNER_PUBLISH_DIR "${CMAKE_CURRENT_BINARY_DIR}/release/visionDesigner")
if(WIN32)
	get_filename_component(QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
	find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
endif()
set(DESIGNER_PUBLISH_COMMANDS
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${DESIGNER_PUBLISH_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${DESIGNER_PUBLISH_DIR}"
	COMMAND ${CMAKE_COMMAND} -E copy
		"$<TARGET_FILE:visionDesigner>" "${DESIGNER_PUBLISH_DIR}/"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${CMAKE_SOURCE_DIR}/Shell/controls"
		"${DESIGNER_PUBLISH_DIR}/Shell/controls"
)
if(WIN32 AND WINDEPLOYQT_EXECUTABLE)
	list(APPEND DESIGNER_PUBLISH_COMMANDS
		COMMAND "${WINDEPLOYQT_EXECUTABLE}"
			--dir "${DESIGNER_PUBLISH_DIR}"
			--qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
			--compiler-runtime
			--no-translations
			"$<TARGET_FILE:visionDesigner>"
	)
endif()
if(NOT WIN32 OR WINDEPLOYQT_EXECUTABLE)
	add_custom_target(publishDesigner ${DESIGNER_PUBLISH_COMMANDS}
		DEPENDS visionDesigner
		VERBATIM
	)
	set_property(TARGET publishDesigner PROPERTY FOLDER "Custom")
endif()
