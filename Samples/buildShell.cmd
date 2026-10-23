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
if not defined QT_DIR if defined QTDIR if exist "%QTDIR%\lib\Qt6Core.lib" set "QT_DIR=%QTDIR%"
if not defined QT_DIR (
	for /d %%R in ("%SystemDrive%\Qt\6.*" "D:\Qt\6.*" "D:\Qt-OpenSource\6.*") do (
		for /d %%Q in ("%%~fR\msvc*_64") do (
			if exist "%%~fQ\lib\Qt6Core.lib" set "QT_DIR=%%~fQ"
		)
	)
)
if not defined QT_DIR (
	echo ERROR: No Qt6 MSVC kit found. Set QT_DIR or VISION_SHELL_QT_DIR.
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
rem Keep Ninja header-dependency detection independent of the Windows display language.
set "VSLANG=1033"
rem MSVC without an English language pack still emits localized include traces.
for /f "tokens=2 delims=:" %%C in ('chcp') do set "ORIGINAL_CODEPAGE=%%C"
chcp 65001 >nul
rem Use the matching VS CMake when available, including localized MSVC dependency support.
set "CMAKE_EXE=cmake"
if defined VSINSTALLDIR if exist "%VSINSTALLDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=%VSINSTALLDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
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
set PRODUCT_OPTION=-DVISION_PRODUCT_SAMPLE="%SAMPLE%" -DVISION_PRODUCT_APPLICATION=
if exist "%ROOT%\Samples\%SAMPLE%\application.cmake" set PRODUCT_OPTION=-DVISION_PRODUCT_SAMPLE= -DVISION_PRODUCT_APPLICATION="%ROOT%\Samples\%SAMPLE%"
echo Building %SAMPLE%: MSVC x64, Ninja, Release
echo Build directory: %BUILD%
if defined QT_DIR echo Qt MSVC kit: %QT_DIR%
"%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD%" -G Ninja -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe -DCMAKE_BUILD_TYPE=Release %PRODUCT_OPTION% -DVISION_BUILD_SERVICE=ON -DVISION_BUILD_SHELL=ON -DVISION_BUILD_DESIGNER=ON -DVISION_BUILD_SAMPLES=OFF %QT_OPTION% %EXTRA%
if errorlevel 1 (
	echo Check QT_DIR and installed backend/camera SDK prerequisites. Qt must be an MSVC kit.
	goto failed
)
for /f "tokens=2 delims==" %%C in ('findstr /b /c:"CMAKE_CXX_COMPILER:" "%BUILD%\CMakeCache.txt"') do set "COMPILER=%%C"
for %%C in ("%COMPILER%") do if /i not "%%~nxC"=="cl.exe" (
	echo ERROR: Windows product Shell requires MSVC.
	goto failed
)
"%CMAKE_EXE%" --build "%BUILD%" --config Release --target publishSampleShell --parallel "%JOBS%"
if errorlevel 1 goto failed
for /f "tokens=2 delims==" %%P in ('findstr /b /c:"VISION_DESIGN_KIT_ROOT:PATH=" "%BUILD%\CMakeCache.txt"') do set "PUBLISH_ROOT=%%P"
echo Design kit updated: %PUBLISH_ROOT%\release
echo Open "%PUBLISH_ROOT%\release\design.cmd" to assemble and publish the final GUI product.
if defined ORIGINAL_CODEPAGE chcp %ORIGINAL_CODEPAGE% >nul
popd
exit /b 0
:failed
echo ERROR: Shell build or publication failed. The previous package was not replaced.
if defined ORIGINAL_CODEPAGE chcp %ORIGINAL_CODEPAGE% >nul
popd
exit /b 1
:help
echo Usage: buildShell.cmd [sample] [CMake configure options]
echo Default sample: anomalyDirectory
echo Samples: eventInspection, businessExports, anomalyDirectory, anomalyHikMvs, anomalyHikMvsMulti, anomalyTensorRtMulti
echo Qt is detected from QTDIR or common Qt install directories.
echo Override QT_DIR and optional VCVARS64 at the top of this script if needed.
echo Environment overrides: VISION_SHELL_QT_DIR, VISION_SHELL_VCVARS64,
echo VISION_SHELL_BUILD_DIR, VISION_SHELL_JOBS
echo Requirements: CMake 3.25+, Ninja, MSVC x64, Qt6 MSVC, selected backend SDK.
echo Output: Publish\release. This replaces the previous design kit after a successful build.
echo No model or image package is required until final publication in Designer.
echo Declarative products automatically generate and bundle their endpoint manifest.
exit /b 0
