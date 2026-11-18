@call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
@pushd "%~dp0"
cl /nologo /O2 /EHsc /std:c++17 reproducer.cpp /Fobaseline.obj /Febaseline.exe
baseline.exe
cl /nologo /O2 /EHsc /std:c++17 /DFORCE_RUNNER_UP reproducer.cpp /Forunnerup.obj /Ferunnerup.exe
runnerup.exe
@popd
