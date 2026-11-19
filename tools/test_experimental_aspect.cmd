@echo off
setlocal
set "VP_VS="
for /f "usebackq tokens=*" %%i in (`call "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -version "[17.0,19.0)" -requires Microsoft.Component.MSBuild -property installationPath`) do set "VP_VS=%%i"
if not "%~1"=="" set "VP_VS=%~1"
if not exist "%VP_VS%\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe" exit /b 1
call "%VP_VS%\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if not exist "x64\Release\ar-shadow-tests" mkdir "x64\Release\ar-shadow-tests"
cl /nologo /O2 /MD /EHsc /std:c++17 tools\test_experimental_aspect.cpp /Fo"x64\Release\ar-shadow-tests\spatial.obj" /Fe"x64\Release\ar-shadow-tests\spatial.exe"
if errorlevel 1 exit /b 1
"x64\Release\ar-shadow-tests\spatial.exe"
if errorlevel 1 exit /b 1
cl /nologo /O2 /MD /EHsc /std:c++17 /DNOMINMAX /I src\VideoProcessor-Lib tools\test_experimental_aspect_worker.cpp "x64\Release\VideoProcessor-Lib.lib" /Fo"x64\Release\ar-shadow-tests\worker.obj" /Fe"x64\Release\ar-shadow-tests\worker.exe" /link /LTCG
if errorlevel 1 exit /b 1
"x64\Release\ar-shadow-tests\worker.exe"
if errorlevel 1 exit /b 1
cl /nologo /O2 /MD /EHsc /std:c++17 /DNOMINMAX /I src\VideoProcessor-Lib tools\test_experimental_aspect_active.cpp "x64\Release\VideoProcessor-Lib.lib" /Fo"x64\Release\ar-shadow-tests\active.obj" /Fe"x64\Release\ar-shadow-tests\active.exe" /link /LTCG
if errorlevel 1 exit /b 1
"x64\Release\ar-shadow-tests\active.exe"
set "VP_RESULT=%ERRORLEVEL%"
popd
exit /b %VP_RESULT%
