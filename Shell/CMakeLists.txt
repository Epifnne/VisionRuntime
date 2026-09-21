set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_add_executable(visionShell
	src/main.cpp
	src/serviceClient.cpp
	src/frameImageProvider.cpp
	include/serviceClient.hpp
	include/frameImageProvider.hpp
)

qt_add_qml_module(visionShell
	URI VisionShell
	VERSION 1.0
	QML_FILES
		qml/main.qml
		controls/NodeView.qml
		controls/ImageView.qml
		controls/StateCard.qml
		controls/CommandButton.qml
		controls/ParameterForm.qml
		controls/StatNumber.qml
		controls/ResultTable.qml
)

target_include_directories(visionShell PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(visionShell PRIVATE
	visionService
	Qt6::Quick
)

# Ensure OpenVINO plugin is built if requested
if(VISION_BUILD_OPENVINO_PLUGIN)
	add_dependencies(visionShell visionBackendOpenVino)
	# Copy plugin to output directory
	add_custom_command(TARGET visionShell POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"$<TARGET_FILE_DIR:visionBackendOpenVino>"
			"$<TARGET_FILE_DIR:visionShell>/plugins/openvino"
		VERBATIM
	)
endif()

# Sync product packages on every build (copy_directory only touches changed
# files; a POST_BUILD step would skip when the exe is up to date)
set(SHELL_PRODUCTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/products")
if(EXISTS "${SHELL_PRODUCTS_DIR}")
	add_custom_target(syncShellProducts ALL
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${SHELL_PRODUCTS_DIR}"
			"$<TARGET_FILE_DIR:visionShell>/products"
		VERBATIM
	)
	add_dependencies(visionShell syncShellProducts)
	set_property(TARGET syncShellProducts PROPERTY FOLDER "Custom")
endif()

# Publish target: self-contained release root with exe + Qt runtime +
# backend plugins + product packages (mirrors Samples/anomalyDirectory)
get_filename_component(QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
if(WINDEPLOYQT_EXECUTABLE)
	set(SHELL_PUBLISH_DIR "${CMAKE_CURRENT_BINARY_DIR}/release/visionShell")
	set(SHELL_PUBLISH_COMMANDS
		COMMAND ${CMAKE_COMMAND} -E rm -rf "${SHELL_PUBLISH_DIR}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${SHELL_PUBLISH_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy
			"$<TARGET_FILE:visionShell>" "${SHELL_PUBLISH_DIR}/"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"$<TARGET_FILE_DIR:visionShell>/plugins" "${SHELL_PUBLISH_DIR}/plugins"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${SHELL_PRODUCTS_DIR}" "${SHELL_PUBLISH_DIR}/products"
		COMMAND "${WINDEPLOYQT_EXECUTABLE}"
			--dir "${SHELL_PUBLISH_DIR}"
			--qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
			--compiler-runtime
			--no-translations
			"$<TARGET_FILE:visionShell>"
	)
	if(VISION_BUILD_OPENVINO_PLUGIN)
		add_custom_target(publishShell ${SHELL_PUBLISH_COMMANDS}
			DEPENDS visionShell visionBackendOpenVino
			VERBATIM
		)
	else()
		add_custom_target(publishShell ${SHELL_PUBLISH_COMMANDS}
			DEPENDS visionShell
			VERBATIM
		)
	endif()
	set_property(TARGET publishShell PROPERTY FOLDER "Custom")
else()
	message(STATUS "windeployqt not found; publishShell target disabled")
endif()
