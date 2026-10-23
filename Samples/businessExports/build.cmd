@echo off
setlocal
call "%~dp0..\buildShell.cmd" businessExports %*
if errorlevel 1 (
	echo Build failed. See the error above.
	pause
	exit /b 1
)
echo Ready. Open Publish\release\design.cmd to edit and publish your application.
pause
