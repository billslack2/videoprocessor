@echo off
setlocal
set "VP_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "VP_POWERSHELL=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%VP_POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0prerequisites\setup-runtime.ps1" %*
set "VP_SETUP_RESULT=%ERRORLEVEL%"
if /I not "%~1"=="-CheckOnly" pause
exit /b %VP_SETUP_RESULT%
