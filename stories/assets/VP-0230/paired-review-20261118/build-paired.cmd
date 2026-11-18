@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
cl /nologo /O2 /MD /EHsc /std:c++17 /DNOMINMAX /I E:\codex\videoprocessor\ar-confidence-shadow-20261117\src\VideoProcessor-Lib paired.cpp E:\codex\videoprocessor\ar-confidence-shadow-20261117\x64\Release\VideoProcessor-Lib.lib /Fe:paired.exe /link /LTCG

