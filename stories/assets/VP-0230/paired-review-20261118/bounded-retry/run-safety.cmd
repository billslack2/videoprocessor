@call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
@pushd "%~dp0"
cl /nologo /O2 /EHsc /std:c++17 spatial.cpp /Fospatial.obj /Fespatial.exe
spatial.exe
cl /nologo /O2 /EHsc /std:c++17 reproducer.cpp /Foreproducer.obj /Fereproducer.exe
reproducer.exe
cl /nologo /O2 /EHsc /std:c++17 safety.cpp /Fosafety.obj /Fesafety.exe
safety.exe
@popd
