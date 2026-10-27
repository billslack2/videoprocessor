$ErrorActionPreference='Stop'
$source='E:\codex\videoprocessor\ar-dynamic-evidence-review-20261026'
$scratch='E:\codex\diagnostics\ar-dynamic-evidence-review-20261026\native-ad-replay'
Import-Module 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
Set-Location -LiteralPath $scratch
$compile=@('/nologo','/std:c++17','/EHsc','/O2','/MD','/Gy','/DNDEBUG','/D_AFXDLL','/DUNICODE','/D_UNICODE','/DNOMINMAX',('/I'+$source+'\src\VideoProcessor-Lib'),('/I'+$source+'\3rdparty\microsoft_directshow_baseclasses'),'/Fe:native-ad-replay.exe','native-ad-replay.cpp',($source+'\src\VideoProcessor-Lib\ActivePictureEvidence.cpp'),($source+'\src\VideoProcessor-Lib\AnalysisLumaSource.cpp'),($source+'\src\VideoProcessor-Lib\ActivePictureDecisionTimeline.cpp'),($source+'\src\VideoProcessor-Lib\ActivePictureTransitionModel.cpp'),($source+'\src\VideoProcessor-Lib\SubtitleBoxDetector.cpp'),'/link','/OPT:REF','/OPT:ICF')
& cl.exe @compile 2>&1 | Tee-Object -FilePath (Join-Path $scratch 'build.log')
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& '.\native-ad-replay.exe' 'C:\Videoprocessor\vp\logs\analysis-20261026-232451-35800-1' $scratch | Tee-Object -FilePath (Join-Path $scratch 'run.log')
exit $LASTEXITCODE





