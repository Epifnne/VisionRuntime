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
	${PROJECT_SOURCE_DIR}/Tools/packageSupport
)

target_link_libraries(visionShell PRIVATE
	visionService
	Qt6::Quick
)
target_compile_definitions(visionShell PRIVATE
	VISION_RUNTIME_VERSION="${PROJECT_VERSION}"
)
if(BUILD_TESTING)
	add_executable(serviceClientRuntimeTest
		tests/serviceClientRuntimeTest.cpp src/serviceClient.cpp include/serviceClient.hpp)
	target_include_directories(serviceClientRuntimeTest PRIVATE include)
	target_link_libraries(serviceClientRuntimeTest PRIVATE
		visionService Qt6::Quick GTest::gtest nlohmann_json::nlohmann_json)
	include(GoogleTest)
	gtest_discover_tests(serviceClientRuntimeTest DISCOVERY_MODE PRE_TEST)
endif()
if(VISION_PRODUCT_SAMPLE)
	target_link_libraries(visionShell PRIVATE nlohmann_json::nlohmann_json)
	target_sources(visionShell PRIVATE
		"${PROJECT_SOURCE_DIR}/Samples/common/productShell.cpp"
		"${VISION_SAMPLE_DIRECTORY}/${VISION_PRODUCT_SAMPLE}Sample.cpp")
	target_include_directories(visionShell PRIVATE
		"${PROJECT_SOURCE_DIR}/Samples/common" "${VISION_SAMPLE_DIRECTORY}")
	target_compile_definitions(visionShell PRIVATE
		VISION_SAMPLE_SHELL=1 VISION_PRODUCT_ID="${VISION_PRODUCT_SAMPLE}")
	include("${PROJECT_SOURCE_DIR}/Samples/common/productPublish.cmake")
endif()
if(WIN32 AND VISION_CAMERA_SDK STREQUAL "HIK_MVS")
	_vision_deploy_hik_mvs_runtime(visionShell)
endif()
file(GENERATE
	OUTPUT "$<TARGET_FILE_DIR:visionShell>/runtime-version.json"
	CONTENT "{\n  \"runtimeVersion\": \"${PROJECT_VERSION}\",\n  \"productRuntime\": \"${VISION_PRODUCT_SAMPLE}\",\n  \"platform\": \"${CMAKE_SYSTEM_NAME}\",\n  \"architecture\": \"${CMAKE_SYSTEM_PROCESSOR}\",\n  \"compiler\": \"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\",\n  \"configuration\": \"$<CONFIG>\",\n  \"cameraSdk\": \"${VISION_CAMERA_SDK}\"\n}\n"
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
if(VISION_BUILD_TENSORRT_PLUGIN)
	add_dependencies(visionShell visionBackendTensorRt)
	add_custom_command(TARGET visionShell POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"$<TARGET_FILE_DIR:visionBackendTensorRt>"
			"$<TARGET_FILE_DIR:visionShell>/plugins/tensorrt"
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
set(SHELL_PUBLISH_DIR "${CMAKE_CURRENT_BINARY_DIR}/release/visionShell")
if(WIN32)
	get_filename_component(QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)
	find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
endif()
if(WIN32 AND WINDEPLOYQT_EXECUTABLE)
	set(SHELL_PUBLISH_COMMANDS
		COMMAND ${CMAKE_COMMAND} -E rm -rf "${SHELL_PUBLISH_DIR}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${SHELL_PUBLISH_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy
			"$<TARGET_FILE:visionShell>" "${SHELL_PUBLISH_DIR}/"
		COMMAND ${CMAKE_COMMAND} -E make_directory
			"${SHELL_PUBLISH_DIR}/plugins"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_SOURCE_DIR}/Runtime/plugins/plugins.json"
			"${SHELL_PUBLISH_DIR}/plugins/plugins.json"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${SHELL_PRODUCTS_DIR}" "${SHELL_PUBLISH_DIR}/products"
		COMMAND "${WINDEPLOYQT_EXECUTABLE}"
			--dir "${SHELL_PUBLISH_DIR}"
			--qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
			--compiler-runtime
			--no-translations
			"$<TARGET_FILE:visionShell>"
	)
	if(VISION_BUILD_OPENVINO_PLUGIN OR VISION_BUILD_TENSORRT_PLUGIN)
		list(APPEND SHELL_PUBLISH_COMMANDS
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"$<TARGET_FILE_DIR:visionShell>/plugins"
				"${SHELL_PUBLISH_DIR}/plugins"
		)
	endif()
else()
	# Linux: there is no windeployqt equivalent; the entry script / package
	# documentation require Qt 6.10+ on the target machine (system packages).
	# The root carries the exe (RUNPATH=$ORIGIN) + backend plugins + products.
	set(SHELL_PUBLISH_COMMANDS
		COMMAND ${CMAKE_COMMAND} -E rm -rf "${SHELL_PUBLISH_DIR}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${SHELL_PUBLISH_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy
			"$<TARGET_FILE:visionShell>" "${SHELL_PUBLISH_DIR}/"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${SHELL_PRODUCTS_DIR}" "${SHELL_PUBLISH_DIR}/products"
	)
	if(VISION_BUILD_OPENVINO_PLUGIN OR VISION_BUILD_TENSORRT_PLUGIN)
		list(APPEND SHELL_PUBLISH_COMMANDS
			COMMAND ${CMAKE_COMMAND} -E copy_directory
				"$<TARGET_FILE_DIR:visionShell>/plugins"
				"${SHELL_PUBLISH_DIR}/plugins"
		)
	endif()
	# Ship the backend capability manifest so visionDesigner can enumerate
	# backends/devices/platforms from the packaged template.
	list(APPEND SHELL_PUBLISH_COMMANDS
		COMMAND ${CMAKE_COMMAND} -E make_directory
			"${SHELL_PUBLISH_DIR}/plugins"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_SOURCE_DIR}/Runtime/plugins/plugins.json"
			"${SHELL_PUBLISH_DIR}/plugins/plugins.json"
	)
endif()
if(NOT WIN32 OR WINDEPLOYQT_EXECUTABLE)
	list(APPEND SHELL_PUBLISH_COMMANDS
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE_DIR:visionShell>/runtime-version.json"
			"${SHELL_PUBLISH_DIR}/runtime-version.json"
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
