$ErrorActionPreference='Stop'
$source='E:\codex\videoprocessor\vp-0218-boundary-research-20261027'
$scratch='E:\codex\diagnostics\vp-0218-boundary-research-20261027\profile\normalized-ablation'
Import-Module 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
Enter-VsDevShell -VsInstallPath 'C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional' -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
Set-Location -LiteralPath $scratch
$compile=@('/nologo','/std:c++17','/EHsc','/O2','/MD','/Gy','/DNDEBUG','/D_AFXDLL','/DUNICODE','/D_UNICODE','/DNOMINMAX',('/I'+$source+'\src\VideoProcessor-Lib'),('/I'+$source+'\3rdparty\microsoft_directshow_baseclasses'),'/Fe:normalized-ablation-probe.exe','normalized-ablation-probe.cpp')
foreach($file in @('ActivePictureEvidence','AnalysisLumaSource','ActivePictureDecisionTimeline','ActivePictureTransitionModel','SubtitleBoxDetector','LocalBoundaryDiagnostics','LocalBoundaryRefinement')){$compile+=($source+'\src\VideoProcessor-Lib\'+$file+'.cpp')}
$compile+=@('/link','/OPT:REF','/OPT:ICF')
& cl.exe @compile 2>&1 | Tee-Object -FilePath (Join-Path $scratch 'build.log')
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& '.\normalized-ablation-probe.exe' $scratch (Join-Path $scratch 'beta-results.csv') | Tee-Object -FilePath (Join-Path $scratch 'run.log')
exit $LASTEXITCODE
