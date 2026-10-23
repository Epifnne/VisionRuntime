@echo off
call "%~dp0..\buildShell.cmd" eventInspection %*
set "RESULT=%errorlevel%"
pause
exit /b %RESULT%
