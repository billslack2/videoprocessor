@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0RUN_ACTIVE_OUTPUT_SWEEP.ps1"
set "EXITCODE=%ERRORLEVEL%"
if not "%EXITCODE%"=="0" echo Active output sweep failed with exit code %EXITCODE%. Review the message above.
echo.
pause
exit /b %EXITCODE%
