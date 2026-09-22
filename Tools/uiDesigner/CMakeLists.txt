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
)

target_link_libraries(visionDesigner PRIVATE
	Qt6::Quick
)
