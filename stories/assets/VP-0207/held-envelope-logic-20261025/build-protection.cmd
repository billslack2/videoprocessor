@copy /y "E:\codex\videoprocessor\subtitle-rewind-ar-20261025\src\VideoProcessor-Lib\ActivePictureEvidence.cpp" "ActivePictureEvidence.cpp" >nul
@copy /y "E:\codex\videoprocessor\subtitle-rewind-ar-20261025\src\VideoProcessor-Lib\AnalysisLumaSource.cpp" "AnalysisLumaSource.cpp" >nul
@copy /y "E:\codex\videoprocessor\subtitle-rewind-ar-20261025\src\VideoProcessor-Lib\SubtitleBoxDetector.cpp" "SubtitleBoxDetector.cpp" >nul
@call "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul
@cl /nologo /DNOMINMAX /D_AFXDLL /O2 /MD /EHsc /Gy /std:c++17 /I include /I "E:\codex\videoprocessor\subtitle-rewind-ar-20261025\src\VideoProcessor-Lib" protection-probe.cpp "E:\codex\videoprocessor\subtitle-rewind-ar-20261025\src\VideoProcessor-Lib\vprenderer\AlphaSourceCropPolicy.cpp" "E:\codex\videoprocessor\subtitle-rewind-ar-20261025\src\VideoProcessor-Lib\ActivePictureDecisionTimeline.cpp" ActivePictureEvidence.cpp "E:\codex\videoprocessor\subtitle-rewind-ar-20261025\src\VideoProcessor-Lib\ActivePictureTransitionModel.cpp" AnalysisLumaSource.cpp SubtitleBoxDetector.cpp /Feprotection-probe.exe /link /OPT:REF
@if errorlevel 1 exit /b %errorlevel%
@protection-probe.exe
