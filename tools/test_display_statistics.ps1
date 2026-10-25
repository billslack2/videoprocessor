[CmdletBinding()]
param(
    [switch]$IncludeCollector,
    [string]$VSTestPath,
    [string]$ResultsDirectory
)
$ErrorActionPreference='Stop'
$vpSource=Split-Path -Parent $PSScriptRoot
$vpBuild=Join-Path $vpSource 'x64\Release'
$vpTests=Join-Path $vpBuild 'VideoProcessor-Test.dll'
if(-not (Test-Path -LiteralPath $vpTests)){throw 'Build the x64 Release solution before running display-statistics tests.'}
if(-not $VSTestPath){
    $vpVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VSTestPath=& $vpVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' | Select-Object -First 1
}
if(-not $VSTestPath -or -not (Test-Path -LiteralPath $VSTestPath)){throw 'A Visual Studio native vstest runner is required.'}
if(-not $ResultsDirectory){$ResultsDirectory=Join-Path $vpSource ('artifacts\display-statistics-tests-'+(Get-Date -Format yyyyMMdd-HHmmss))}
$null=New-Item -ItemType Directory -Path $ResultsDirectory -Force
$vpFilter='FullyQualifiedName~PassiveDisplayStatisticsTests|FullyQualifiedName~PassiveDisplayProtocolTests|FullyQualifiedName~PassiveDisplayObserverTests|FullyQualifiedName~StatsOverlayLayoutTests|FullyQualifiedName~PlaybackCounterWindowTests|FullyQualifiedName~OutputPresentationPublicationTests|FullyQualifiedName~AlphaOutputPresentationTelemetryTests|FullyQualifiedName~PhysicalInputResyncLatchTests'
$vpPreviousObserver=$env:VP_DISPLAY_OBSERVER
$vpPreviousIntegration=$env:VP_TEST_PASSIVE_OBSERVER
$vpExit=1
Push-Location -LiteralPath $vpBuild
try {
    $env:VP_DISPLAY_OBSERVER='0'
    $env:VP_TEST_PASSIVE_OBSERVER='0'
    if($IncludeCollector){
        # Test DLL resolves its helper beside itself. This only stages a build
        # artifact; it never touches the deployment directory or configuration.
        $vpHelper=Join-Path $vpBuild 'vprenderer\VideoProcessorDisplayObserver.exe'
        $vpRecord=Get-Content -LiteralPath ($vpHelper+'.runtime.json') -Raw | ConvertFrom-Json
        if($vpRecord.configuration -ne 'Release' -or $vpRecord.architecture -ne 'x64' -or
            $vpRecord.sha256 -ne (Get-FileHash -LiteralPath $vpHelper -Algorithm SHA256).Hash){throw 'Observer x64 Release build receipt is missing or stale.'}
        Copy-Item -LiteralPath $vpHelper -Destination (Join-Path $vpBuild 'VideoProcessorDisplayObserver.exe')
        $env:VP_TEST_PASSIVE_OBSERVER='1'
    } else {$vpFilter='('+$vpFilter+')&FullyQualifiedName!~OptInCollector'}
    & $VSTestPath $vpTests /Platform:x64 "/TestCaseFilter:$vpFilter" '/Logger:trx;LogFileName=display-statistics.trx' "/ResultsDirectory:$ResultsDirectory"
    $vpExit=$LASTEXITCODE
} finally {
    $env:VP_DISPLAY_OBSERVER=$vpPreviousObserver
    $env:VP_TEST_PASSIVE_OBSERVER=$vpPreviousIntegration
    Pop-Location
}
if($vpExit -ne 0){throw "Display-statistics verification failed (exit $vpExit)."}
Write-Host "Display-statistics checks passed. Real collector lifecycle exercised: $IncludeCollector. This does not establish physical-display acceptance."
