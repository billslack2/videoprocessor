$ErrorActionPreference = 'Stop'
$root = 'E:\codex\videoprocessor\vp-0206-merge-check-20261023'
Set-Location -LiteralPath $root
$expected = 'c48eb134c34a93c762e026af6daf50aaeb1dc83c'
$actual = (& git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actual -ne $expected) { throw 'Unexpected source' }
if (& git status --porcelain) { throw 'Dirty source' }
$runner = 'C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\Extensions\TestPlatform\vstest.console.exe'
$dll = Join-Path $root 'x64\Release\VideoProcessor-Test.dll'
$evidence = Join-Path $root 'artifacts\vp-0206-merge'
$filter = 'FullyQualifiedName~ActivePicture|FullyQualifiedName~AlphaSourceCropPolicyTests|FullyQualifiedName~FinalAspectLimitFillRoutingTests|FullyQualifiedName~PendingOverlayEnvelopeTests|FullyQualifiedName~SubmittedNativeScope|FullyQualifiedName~UncertainPicture|FullyQualifiedName~RetainedScopeUnknownPresentationTests|FullyQualifiedName~SubmittedNativeFillRetentionTests'
& $runner $dll '/Platform:x64' "/TestCaseFilter:$filter" "/ResultsDirectory:$evidence\targeted-results" '/Logger:trx;LogFileName=standalone-targeted.trx' *> "$evidence\standalone-targeted.log"
$targetExit = $LASTEXITCODE
Get-Content -LiteralPath "$evidence\standalone-targeted.log" -Tail 12
if ($targetExit -ne 0) { throw "Targeted regressions failed ($targetExit)" }
try {
    $env:VP_BLACK_REFERENCE_REPLAY_DIRECTORY = 'C:\Videoprocessor\vp\logs\analysis-20261023-122213-68268-1'
    & $runner $dll '/Platform:x64' '/TestCaseFilter:FullyQualifiedName~OptionalNativeBlackReferenceCaptureReleasesSubmittedScope' "/ResultsDirectory:$evidence\capture-results" '/Logger:trx;LogFileName=standalone-capture.trx' *> "$evidence\standalone-capture.log"
    $captureExit = $LASTEXITCODE
} finally { Remove-Item Env:VP_BLACK_REFERENCE_REPLAY_DIRECTORY -ErrorAction SilentlyContinue }
Get-Content -LiteralPath "$evidence\standalone-capture.log" -Tail 14
if ($captureExit -ne 0) { throw "Captured-scene replay failed ($captureExit)" }
$runs = @()
foreach ($pair in @(@('targeted','targeted-results\standalone-targeted.trx'),@('capture','capture-results\standalone-capture.trx'))) {
    $path = Join-Path $evidence $pair[1]
    [xml]$trx = Get-Content -LiteralPath $path -Raw
    $counter = $trx.TestRun.ResultSummary.Counters
    if ([int]$counter.failed -ne 0 -or [int]$counter.executed -lt 1 -or [int]$counter.passed -ne [int]$counter.executed) { throw 'Incomplete test result' }
    $runs += [pscustomobject]@{ name=$pair[0]; file=$pair[1]; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash; executed=[int]$counter.executed; passed=[int]$counter.passed; failed=[int]$counter.failed }
}
$receipt = [ordered]@{ sourceCommit=$expected; betaCommit='f7c7a8204f7a2cace68b3b40774406680af6c0d8'; configuration='Release'; architecture='x64'; buildExitCode=0; testDllSha256=(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash; buildLogSha256=(Get-FileHash -LiteralPath "$evidence\standalone-release-build.log" -Algorithm SHA256).Hash; testRuns=$runs; sourceClean=(-not [bool](& git status --porcelain)); deploymentChanged=$false; priorUnfilteredSweep='Interrupted; no full-suite pass claimed' }
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$evidence\standalone-validation.json" -Encoding utf8
$receipt | ConvertTo-Json -Depth 6
