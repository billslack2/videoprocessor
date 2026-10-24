@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
MSBuild.exe VideoProcessor.sln /t:Rebuild /m:1 /p:Configuration=Release /p:Platform=x64 /p:MagewellSdkRoot=E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3 /v:minimal /fl /flp:logfile=artifacts\pr193-prepared-split-20261023\clean-rebuild\release-build.log;verbosity=normal
exit /b %errorlevel%