@echo off
setlocal
set "VP_VS=C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional"
if not "%~1"=="" set "VP_VS=%~1"
call "%VP_VS%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
set "VP_RELATIVE_OUT=%TEMP%\vp-relative-continuation-tests"
if not exist "%VP_RELATIVE_OUT%" mkdir "%VP_RELATIVE_OUT%"
pushd "%~dp0.."
cl /nologo /O2 /MD /EHsc /std:c++17 /DNOMINMAX /I src\VideoProcessor-Lib tools\test_relative_continuation.cpp "x64\Release\VideoProcessor-Lib.lib" /Fo"%VP_RELATIVE_OUT%\relative.obj" /Fd"%VP_RELATIVE_OUT%\compile.pdb" /Fe"%VP_RELATIVE_OUT%\relative.exe" /link /LTCG /PDB:"%VP_RELATIVE_OUT%\link.pdb"
if errorlevel 1 (popd & exit /b 1)
"%VP_RELATIVE_OUT%\relative.exe"
set "VP_RELATIVE_RESULT=%ERRORLEVEL%"
popd
exit /b %VP_RELATIVE_RESULT%
