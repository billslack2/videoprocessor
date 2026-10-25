$ErrorActionPreference='Stop'
$vpSource='E:\codex\videoprocessor\pr177-pr203-deploy-20261025'
$vpEvidence='E:\codex\diagnostics\pr177-pr203-deploy-20261025'
$vpExpected='6b02334a83100645e8331d89f63a47797111e79f'
if((& git -C $vpSource rev-parse HEAD).Trim() -ne $vpExpected -or @(& git -C $vpSource status --porcelain).Count){throw 'Final source identity is not clean and pinned'}
[xml]$vpTrx=Get-Content -LiteralPath (Join-Path $vpEvidence 'off-related.trx') -Raw
$vpCounters=$vpTrx.TestRun.ResultSummary.Counters
if([int]$vpCounters.total -ne 684 -or [int]$vpCounters.passed -ne 684 -or [int]$vpCounters.failed -ne 0){throw 'Native validation incomplete'}
$vpConfig=Get-Content -LiteralPath (Join-Path $vpEvidence 'config-results.json') -Raw | ConvertFrom-Json
if($vpConfig.Count -ne 24 -or @($vpConfig | Where-Object {!$_.Passed -or $_.Exit -ne 0}).Count){throw 'Config validation incomplete'}
$vpScales=Get-Content -LiteralPath (Join-Path $vpEvidence 'layout-scale-results.json') -Raw | ConvertFrom-Json
if($vpScales.Count -ne 2 -or @($vpScales | Where-Object {!$_.Passed -or $_.Exit -ne 0}).Count){throw 'Additional scale validation incomplete'}
$vpBinaries=@('VideoProcessor-GUI.exe','VideoProcessorConfig.exe','VideoProcessorConfigDiscovery.dll','vprenderer\VideoProcessorVPRenderer.dll','vprenderer-beta\VideoProcessorVPRendererBeta.dll','VideoProcessor-Test.dll','VideoProcessorConfigTests.exe')
$vpBinaryRecords=@(foreach($vpRelative in $vpBinaries){
 $vpBinary=Join-Path (Join-Path $vpSource 'x64\Release') $vpRelative
 $vpReceipt=Get-Content -LiteralPath ($vpBinary+'.runtime.json') -Raw | ConvertFrom-Json
 $vpHash=(Get-FileHash -LiteralPath $vpBinary).Hash
 if($vpReceipt.configuration -ne 'Release' -or $vpReceipt.architecture -ne 'x64' -or $vpReceipt.sha256 -ne $vpHash){throw "Build receipt mismatch: $vpBinary"}
 [pscustomobject]@{Path=$vpRelative;SHA256=$vpHash;Receipt=$vpReceipt}
})
foreach($vpHeader in @('src\VideoProcessor-GUI\version.h','src\VideoProcessor-VPRenderer\version.h')){
 $vpIdentity=[IO.File]::ReadAllText((Join-Path $vpSource $vpHeader))
 if(!$vpIdentity.Contains('/commit/'+$vpExpected) -or !$vpIdentity.Contains('VERSION_DIRTY=false')){throw 'Generated runtime source identity mismatch'}
}
$vpRecord=[pscustomobject]@{
 SourceCommit=$vpExpected;SourceBranch='codex/pr177-pr203-deploy-20261025';SourceClean=$true
 BetaBranch='v1.4.02-beta';BetaCommit='8c4537462bbabd6ce07ba3e9ffd9be9bc8745492'
 PR177Original='2a0f4763ca60de87cef7d1d8f5e4c9e29060580c';PR177MigrationIntegration='588f9a9e91a4bd83e1685e2e7a5cebc92b0147f8';PR177ResponsiveFollowUp='83ff62acc4557dde176687db68609e24ebf7149d'
 PR203OffCommit='697c1bcd779891cadde993528c03a71f473e0653'
 BuildExitCode=0;Configuration='Release';Architecture='x64';BuildLog='release-build.log'
 NativePassed=[int]$vpCounters.passed;NativeFailed=[int]$vpCounters.failed;ConfigPassed=$vpConfig.Count;ConfigFailed=0
 RealChartRuntimeRequired=$true;ResponsiveScales=@('1','1.5','2');WindowsFonts='C:\Windows\Fonts';QtVersion='6.8.3'
 Binaries=$vpBinaryRecords;ValidatedAt=(Get-Date).ToString('o')
 InitialHarnessFailure='Preserved in initial-real-chart-layout-failure. The offscreen harness omitted QT_QPA_FONTDIR. With Windows fonts, the exact same clean built executable passed at all three scales. No source change or rebuild was made to bypass the failure.'
 DeploymentStatus='Ready; awaiting the user closing the installed Config editor'
 ConfigurationEdits=@();LiveCaptureAcceptance='Pending; automated tests do not establish GPU/capture stability or performance'
}
$vpRecord | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $vpEvidence 'final-validation.json') -Encoding utf8
$vpRecord | Select-Object SourceCommit,NativePassed,NativeFailed,ConfigPassed,ConfigFailed,DeploymentStatus | ConvertTo-Json