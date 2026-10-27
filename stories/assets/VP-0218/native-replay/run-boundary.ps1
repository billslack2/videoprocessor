$ErrorActionPreference='Stop'
$source='E:\codex\videoprocessor\ar-dynamic-evidence-review-20261026'
$scratch='E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay'
Import-Module 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
Set-Location -LiteralPath $scratch
& cl.exe /nologo /std:c++17 /EHsc /O2 /MD /Gy /DNDEBUG /D_AFXDLL /DUNICODE /D_UNICODE /DNOMINMAX ('/I'+$source+'\src\VideoProcessor-Lib') /Fe:boundary-probe.exe boundary-probe.cpp ActivePictureEvidence.obj AnalysisLumaSource.obj ActivePictureDecisionTimeline.obj ActivePictureTransitionModel.obj SubtitleBoxDetector.obj /link /OPT:REF /OPT:ICF 2>&1 | Tee-Object -FilePath 'boundary-build.log'
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& '.\boundary-probe.exe' 'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1' $scratch | Tee-Object -FilePath 'boundary-run.log'
exit $LASTEXITCODE
