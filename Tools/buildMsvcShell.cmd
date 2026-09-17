@echo off
rem Build (or reconfigure+build) targets in the Build/MSVC-2026 tree.
rem Usage: buildMsvcShell.cmd [target ...]   (default target: visionShell)
rem Parallelism is capped at 4 jobs to limit CPU temperature.
setlocal
call "D:\Visual Studio\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b 1
set "CMAKE_EXE=D:\Qt\Tools\CMake_64\bin\cmake.exe"
set "BUILD_DIR=E:\Work\VisionRuntime\Build\MSVC-2026"
if "%~1"=="" (
	set "TARGET=visionShell"
) else (
	set "TARGET=%*"
)
"%CMAKE_EXE%" --build "%BUILD_DIR%" --target %TARGET% --parallel 4
exit /b %errorlevel%
