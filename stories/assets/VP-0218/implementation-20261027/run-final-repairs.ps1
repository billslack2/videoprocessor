Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$sourceRoot='E:\codex\videoprocessor\vp-0218-adaptive-ar-20261027'
$root='E:\codex\diagnostics\vp-0218-implementation-20261027\candidate'
$expected='ddf5d666803c29e0bdf7af310963b2695e382bc2'
$buildPath=Join-Path $root 'release-build-result.json'
$build=Get-Content -LiteralPath $buildPath -Raw | ConvertFrom-Json
if($build.sourceCommit -ne $expected -or $build.exitCode -ne 0 -or $build.configuration -ne 'Release' -or $build.architecture -ne 'x64'){throw 'Final successful pinned build is required'}
if((& git -C $sourceRoot rev-parse HEAD).Trim() -ne $expected -or @(& git -C $sourceRoot status --porcelain).Count){throw 'Final source is not clean and pinned'}
$assembly=Join-Path $sourceRoot 'x64\Release\VideoProcessor-Test.dll'
$assemblyHash=(Get-FileHash -LiteralPath $assembly -Algorithm SHA256).Hash
if($assemblyHash -ne $build.testAssemblySha256){throw 'Test assembly differs from final build'}
$trxPath=Join-Path $root 'final-repairs.trx'
if(Test-Path -LiteralPath $trxPath){throw 'Preserve existing retest evidence before another attempt'}
$buildEvidencePath=Join-Path $root 'final-build-test-evidence.json'
[ordered]@{sourceCommit=$expected;configuration='Release';architecture='x64';exitCode=0;completedUtc=$build.completedUtc;buildRecord=$buildPath;buildRecordSha256=(Get-FileHash -LiteralPath $buildPath).Hash;testAssembly=$assembly;testAssemblySha256=$assemblyHash} | ConvertTo-Json | Set-Content -LiteralPath $buildEvidencePath -Encoding UTF8
$runId=[Guid]::NewGuid().ToString()
$testExe='C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe'
$testArguments=@($assembly,'/Platform:x64','/TestCaseFilter:FullyQualifiedName~FitLookaheadDiagnosticTests|FullyQualifiedName~BlackPanelCoversLastPictureRowWithSquareLowerCorners|FullyQualifiedName~MeasuredCardEdgesRestoreOutsideSamplingRectangleAtEveryDisplayPadding',('/ResultsDirectory:'+$root),'/Logger:trx;LogFileName=final-repairs.trx')
[ordered]@{sourceCommit=$expected;testAssembly=$assembly;testAssemblySha256=$assemblyHash;buildEvidence=$buildEvidencePath;buildEvidenceSha256=(Get-FileHash -LiteralPath $buildEvidencePath).Hash;preparedUtc=[DateTimeOffset]::UtcNow.ToString('o');runId=$runId;executable=$testExe;arguments=$testArguments}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath ($trxPath+'.input.json') -Encoding UTF8
& $testExe @testArguments 2>&1 | Tee-Object -FilePath (Join-Path $root 'final-repairs.log')
$testExit=$LASTEXITCODE
$afterHash=(Get-FileHash -LiteralPath $assembly -Algorithm SHA256).Hash
$afterHead=(& git -C $sourceRoot rev-parse HEAD).Trim()
if($afterHash -ne $assemblyHash -or $afterHead -ne $expected -or @(& git -C $sourceRoot status --porcelain).Count){$testExit=99}
[ordered]@{runId=$runId;sourceCommit=$afterHead;testAssemblySha256=$afterHash;exitCode=$testExit;completedUtc=[DateTimeOffset]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath ($trxPath+'.result.json') -Encoding UTF8
exit $testExit