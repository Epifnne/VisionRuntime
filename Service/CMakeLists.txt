add_library(visionService STATIC
	src/camera/cameraService.cpp
	src/endpoints/endpointDispatcher.cpp
	src/endpoints/endpointRegistry.cpp
	src/manifest/manifestExporter.cpp
	src/metrics/metricsAggregator.cpp
	src/profile/profileLoader.cpp
	src/service/visionService.cpp
	src/session/sessionAssembler.cpp
	src/session/sessionController.cpp
)

target_compile_features(visionService PUBLIC cxx_std_20)

if(MSVC)
	target_compile_options(visionService PRIVATE /W4 /permissive-)
else()
	target_compile_options(visionService PRIVATE
		-Wall
		-Wextra
		-Wpedantic
		-Wconversion
		-Wsign-conversion
	)
endif()

target_include_directories(visionService
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(visionService
	PUBLIC
		visionRuntime
	PRIVATE
		nlohmann_json::nlohmann_json
)
