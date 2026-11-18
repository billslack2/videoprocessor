@echo off
setlocal
set "VP_VS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional"
if not "%~1"=="" set "VP_VS=%~1"
call "%VP_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
pushd "%~dp0.."
if not exist "x64\Release\ar-shadow-tests" mkdir "x64\Release\ar-shadow-tests"
cl /nologo /O2 /MD /EHsc /std:c++17 tools\test_experimental_aspect.cpp /Fo"x64\Release\ar-shadow-tests\spatial.obj" /Fe"x64\Release\ar-shadow-tests\spatial.exe"
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
"x64\Release\ar-shadow-tests\spatial.exe"
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
cl /nologo /O2 /MD /EHsc /std:c++17 /DNOMINMAX tools\test_experimental_aspect_mode.cpp /Fo"x64\Release\ar-shadow-tests\mode.obj" /Fe"x64\Release\ar-shadow-tests\mode.exe"
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
"x64\Release\ar-shadow-tests\mode.exe"
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
cl /nologo /O2 /MD /EHsc /std:c++17 /DNOMINMAX /I src\VideoProcessor-Lib tools\test_experimental_aspect_worker.cpp "x64\Release\VideoProcessor-Lib.lib" /Fo"x64\Release\ar-shadow-tests\worker.obj" /Fe"x64\Release\ar-shadow-tests\worker.exe" /link /LTCG
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
"x64\Release\ar-shadow-tests\worker.exe"
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
cl /nologo /O2 /MD /EHsc /std:c++17 /DNOMINMAX /I src\VideoProcessor-Lib tools\test_experimental_aspect_active.cpp "x64\Release\VideoProcessor-Lib.lib" /Fo"x64\Release\ar-shadow-tests\active.obj" /Fe"x64\Release\ar-shadow-tests\active.exe" /link /LTCG
if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%
"x64\Release\ar-shadow-tests\active.exe"
set "VP_RESULT=%ERRORLEVEL%"
popd
exit /b %VP_RESULT%
