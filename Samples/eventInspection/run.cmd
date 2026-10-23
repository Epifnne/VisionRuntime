@echo off
cd /d "%~dp0"
eventInspectionSample.exe profile.json %*
set "RESULT=%errorlevel%"
pause
exit /b %RESULT%
