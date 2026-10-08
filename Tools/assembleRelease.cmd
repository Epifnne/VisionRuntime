@echo off
rem Assemble the redistributable design-time release:
rem   Publish\release\
rem     visionDesigner\            Windows designer (windeployqt self-contained)
rem     runtimeTemplates\windows\  visionShell runtime root (publishShell)
rem     runtimeTemplates\linux\    visionShell runtime root (WSL publishShell)
rem Requires: publishShell + publishDesigner built in Build\Release (MinGW)
rem and Build/Linux-GNU-Release (WSL, mounted at \\wsl$\Ubuntu or /mnt/e).
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\Publish\release"
set "SRC=%ROOT%"

if not "%~1"=="" set "OUT=%~1"

if exist "%OUT%" (
	echo ERROR: Output already exists. Choose a new versioned output directory.
	exit /b 1
)
if not exist "%SRC%\Build\Release\Tools\uiDesigner\release\visionDesigner\visionDesigner.exe" (
	echo ERROR: Build publishDesigner first.
	exit /b 1
)
if not exist "%SRC%\Build\Release\Shell\release\visionShell\visionShell.exe" (
	echo ERROR: Build publishShell first.
	exit /b 1
)
echo == Assembling %OUT%
mkdir "%OUT%"
if errorlevel 1 exit /b 1

robocopy "%SRC%\Build\Release\Tools\uiDesigner\release\visionDesigner" "%OUT%\visionDesigner" /e /nfl /ndl /njh /njs
if errorlevel 8 exit /b 1
robocopy "%SRC%\Build\Release\Shell\release\visionShell" "%OUT%\runtimeTemplates\windows" /e /nfl /ndl /njh /njs
if errorlevel 8 exit /b 1

rem The Linux template lives in the WSL-built tree, but that tree is on the
rem Windows drive (/mnt/e), so copy it directly by path — no WSL needed and
rem no fragile \\wsl$ hop. The executable bit was already set by the WSL
rem build and is preserved on the source file.
set "LINUX_TEMPLATE=%SRC%\Build\Linux-GNU-Release\Shell\release\visionShell"
if exist "%LINUX_TEMPLATE%\visionShell" (
	robocopy "%LINUX_TEMPLATE%" "%OUT%\runtimeTemplates\linux" /e /nfl /ndl /njh /njs
	if errorlevel 8 exit /b 1
) else (
	echo WARNING: Linux publishShell not built yet; skipping runtimeTemplates\linux
)

echo == Done. Ship %OUT% to design machines.
endlocal
exit /b 0
