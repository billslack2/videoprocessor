@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
cl /nologo /O2 /EHsc /std:c++17 budget-risk-probe.cpp /Fe:budget-risk-probe.exe
if errorlevel 1 exit /b 1
budget-risk-probe.exe > budget-risk-probe.json
exit /b %errorlevel%