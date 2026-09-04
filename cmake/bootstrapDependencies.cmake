function(cloneDependency name repository revision destination)
	if(EXISTS "${destination}/.git")
		execute_process(
			COMMAND "${GIT_EXECUTABLE}" -C "${destination}" rev-parse HEAD
			OUTPUT_VARIABLE currentRevision
			OUTPUT_STRIP_TRAILING_WHITESPACE
			COMMAND_ERROR_IS_FATAL ANY
		)
		if(currentRevision STREQUAL revision)
			message(STATUS "${name} already matches ${revision}")
			return()
		endif()
		message(FATAL_ERROR
			"${name} at ${destination} has revision ${currentRevision}; expected ${revision}")
	endif()

	get_filename_component(parentDirectory "${destination}" DIRECTORY)
	file(MAKE_DIRECTORY "${parentDirectory}")
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" clone --filter=blob:none --no-checkout
			"${repository}" "${destination}"
		COMMAND_ERROR_IS_FATAL ANY
	)
	execute_process(
		COMMAND "${GIT_EXECUTABLE}" -C "${destination}" checkout --detach "${revision}"
		COMMAND_ERROR_IS_FATAL ANY
	)
endfunction()

if(NOT GIT_EXECUTABLE)
	message(FATAL_ERROR "GIT_EXECUTABLE is required")
endif()

cloneDependency(
	"OpenCV 4.12.0"
	"https://github.com/opencv/opencv.git"
	"49486f61fb25722cbcf586b7f4320921d46fb38e"
	"${THIRDPARTY_DIRECTORY}/opencv/4.12.0"
)
cloneDependency(
	"GoogleTest 1.17.0"
	"https://github.com/google/googletest.git"
	"52eb8108c5bdec04579160ae17225d66034bd723"
	"${THIRDPARTY_DIRECTORY}/gtest/1.17.0"
)
cloneDependency(
	"nlohmann/json 3.12.0"
	"https://github.com/nlohmann/json.git"
	"55f93686c01528224f448c19128836e7df245f72"
	"${THIRDPARTY_DIRECTORY}/json/3.12.0"
)
cloneDependency(
	"spdlog 1.15.3"
	"https://github.com/gabime/spdlog.git"
	"6fa36017cfd5731d617e1a934f0e5ea9c4445b13"
	"${THIRDPARTY_DIRECTORY}/spdlog/1.15.3"
)
cloneDependency(
	"FAISS 1.12.0"
	"https://github.com/facebookresearch/faiss.git"
	"e8234e563f1ecef5f036e83c3cfee366d3f1fbca"
	"${THIRDPARTY_DIRECTORY}/faiss/1.12.0"
)
cloneDependency(
	"OpenBLAS 0.3.30"
	"https://github.com/OpenMathLib/OpenBLAS.git"
	"993fad6aebbce34a97d3f8c34d6d79d35b64cc48"
	"${THIRDPARTY_DIRECTORY}/openblas/0.3.30"
)