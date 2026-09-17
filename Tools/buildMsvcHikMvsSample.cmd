@echo off
rem Builds the anomalyHikMvsSample target in the standalone MSVC sample tree.
rem The tree is configured from Samples/anomalyHikMvs with:
rem   cmake -S Samples/anomalyHikMvs -B Build/MSVC-HikMvs -G Ninja ^
rem     -DCMAKE_BUILD_TYPE=Debug -DVISION_RUNTIME_ROOT=<repo root> ^
rem     -DVISION_BUILD_OPENVINO_PLUGIN=ON
rem Parallelism is capped at 4 jobs to limit CPU temperature.
call "D:\Visual Studio\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 >nul
if errorlevel 1 exit /b 1
"D:\Qt\Tools\CMake_64\bin\cmake.exe" --build "%~dp0..\Build\MSVC-HikMvs" --target anomalyHikMvsSample --parallel 4
exit /b %errorlevel%
