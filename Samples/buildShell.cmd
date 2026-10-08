@echo off
setlocal

rem ===== Machine configuration: edit these values, or use the named environment variables. =====
rem Qt 6 MSVC kit root, e.g. C:\Qt\6.10.1\msvc2022_64. Never select a MinGW kit.
set "QT_DIR="
rem Optional full path to VC\Auxiliary\Build\vcvars64.bat for a custom VS installation.
rem Leave empty to use an existing developer prompt or discover Visual Studio with vswhere.
set "VCVARS64="
rem Builds use MSVC x64, Ninja, Release, and four parallel jobs. No CMake preset is used.
rem Backend/source defaults come from the selected sample's shell.cmake.
rem Output: Publish\release contains Designer and the dedicated runtime template.
rem Models, images, GUI editing, and final product publication belong to Designer.
rem SDK overrides, if not using repository Thirdparty packages:
rem -DTENSORRT_ROOT="C:\SDK\TensorRT" -DOpenVINO_DIR="C:\SDK\openvino\runtime\cmake"
rem -DHIK_MVS_ROOT="C:\SDK\hik-mvs"
set "JOBS=4"
if defined VISION_SHELL_QT_DIR set "QT_DIR=%VISION_SHELL_QT_DIR%"
if defined VISION_SHELL_VCVARS64 set "VCVARS64=%VISION_SHELL_VCVARS64%"
if defined VISION_SHELL_JOBS set "JOBS=%VISION_SHELL_JOBS%"

set "ROOT=%~dp0.."
set "SAMPLE=anomalyDirectory"
if "%~1"=="--help" goto help
if "%~1"=="-h" goto help
set "EXTRA=%*"
if not "%~1"=="" set "SAMPLE=%~1"
if "%~2"=="" (set "EXTRA=") else (set "EXTRA=%EXTRA:* =%")
echo(%SAMPLE%| findstr /r /x "[A-Za-z0-9][A-Za-z0-9_.-]*" >nul
if errorlevel 1 (
	echo ERROR: Invalid sample name.
	exit /b 1
)
set "BUILD=%ROOT%\Build\SampleShell\%SAMPLE%"
if defined VISION_SHELL_BUILD_DIR set "BUILD=%VISION_SHELL_BUILD_DIR%"
pushd "%ROOT%"
if errorlevel 1 exit /b 1
if not defined QT_DIR (
	echo ERROR: Set QT_DIR at the top of this script, or set VISION_SHELL_QT_DIR.
	echo It must point to a Qt6 MSVC kit, e.g. C:\Qt\6.10.1\msvc2022_64.
	goto failed
)
if not exist "%QT_DIR%\lib\Qt6Core.lib" (
	echo ERROR: QT_DIR is not a complete Qt6 MSVC kit: %QT_DIR%
	goto failed
)

if defined INCLUDE if defined LIB (
	where cl.exe >nul 2>nul
	if not errorlevel 1 goto msvcReady
)
if defined VCVARS64 goto initializeMsvc
if defined VSINSTALLDIR set "VCVARS64=%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
if defined VCVARS64 goto initializeMsvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
	for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS64=%%V\VC\Auxiliary\Build\vcvars64.bat"
)
:initializeMsvc
if not exist "%VCVARS64%" (
	echo ERROR: Cannot find MSVC x64 build tools.
	echo Set VCVARS64 at the top of this script, set VISION_SHELL_VCVARS64,
	echo or run from an x64 Visual Studio developer command prompt.
	goto failed
)
call "%VCVARS64%"
if errorlevel 1 goto failed
:msvcReady
where cl.exe >nul 2>nul
if errorlevel 1 (
	echo ERROR: MSVC environment initialization did not provide cl.exe.
	goto failed
)
where ninja.exe >nul 2>nul
if errorlevel 1 (
	if not exist "%VSINSTALLDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" (
		echo ERROR: Install Ninja or add ninja.exe to PATH.
		goto failed
	)
	set "PATH=%VSINSTALLDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
)
set "QT_OPTION="
if defined QT_DIR set QT_OPTION=-DCMAKE_PREFIX_PATH="%QT_DIR%"
echo Building %SAMPLE%: MSVC x64, Ninja, Release
echo Build directory: %BUILD%
if defined QT_DIR echo Qt MSVC kit: %QT_DIR%
cmake -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe -DCMAKE_BUILD_TYPE=Release -DVISION_PRODUCT_SAMPLE="%SAMPLE%" -DVISION_BUILD_DESIGNER=ON -DVISION_BUILD_SAMPLES=OFF %QT_OPTION% %EXTRA%
if errorlevel 1 (
	echo Check QT_DIR and installed backend/camera SDK prerequisites. Qt must be an MSVC kit.
	goto failed
)
for /f "tokens=2 delims==" %%C in ('findstr /b /c:"CMAKE_CXX_COMPILER:" "%BUILD%\CMakeCache.txt"') do set "COMPILER=%%C"
for %%C in ("%COMPILER%") do if /i not "%%~nxC"=="cl.exe" (
	echo ERROR: Windows product Shell requires MSVC.
	goto failed
)
cmake --build "%BUILD%" --config Release --target publishSampleShell --parallel "%JOBS%"
if errorlevel 1 goto failed
echo Design kit updated: %ROOT%\Publish\release
echo Open Publish\release\design.cmd to assemble and publish the final GUI product.
popd
exit /b 0
:failed
echo ERROR: Shell build or publication failed. The previous package was not replaced.
popd
exit /b 1
:help
echo Usage: buildShell.cmd [sample] [CMake configure options]
echo Default sample: anomalyDirectory
echo Samples: anomalyDirectory, anomalyHikMvs, anomalyHikMvsMulti, anomalyTensorRtMulti
echo Configure QT_DIR and optional VCVARS64 at the top of this script.
echo Environment overrides: VISION_SHELL_QT_DIR, VISION_SHELL_VCVARS64,
echo VISION_SHELL_BUILD_DIR, VISION_SHELL_JOBS
echo Requirements: CMake 3.25+, Ninja, MSVC x64, Qt6 MSVC, selected backend SDK.
echo Output: Publish\release. This replaces the previous design kit after a successful build.
echo No model or image package is required until final publication in Designer.
exit /b 0
