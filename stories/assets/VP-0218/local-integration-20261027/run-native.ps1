$ErrorActionPreference='Stop'
$sourceRoot='E:\codex\videoprocessor\vp-0218-local-integration-20261027';$out='E:\codex\diagnostics\vp-0218-local-integration-20261027';$expected='f4219a7aa679c57f1f6d637cb08406b9c2b4b97b'
$build=Get-Content -LiteralPath (Join-Path $out 'build-result.json') -Raw|ConvertFrom-Json
if($build.exitCode -ne 0 -or $build.sourceCommit -ne $expected -or @(& git -C $sourceRoot status --porcelain).Count){throw 'Matching successful clean Release build required'}
$dll=Join-Path $sourceRoot 'x64\Release\VideoProcessor-Test.dll';$hash=(Get-FileHash -LiteralPath $dll).Hash
$args=@($dll,'/Platform:x64','/InIsolation',('/ResultsDirectory:'+$out),'/Logger:trx;LogFileName=combined-native-full.trx')
[ordered]@{sourceCommit=$expected;testAssembly=$dll;testAssemblySha256=$hash;startedUtc=[DateTimeOffset]::UtcNow.ToString('o');arguments=$args}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $out 'native-test-invocation.json') -Encoding utf8
& 'C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' @args *> (Join-Path $out 'native-full.log')
$rc=$LASTEXITCODE
if((Get-FileHash -LiteralPath $dll).Hash -ne $hash -or (& git -C $sourceRoot rev-parse HEAD).Trim() -ne $expected){$rc=99}
[ordered]@{sourceCommit=$expected;testAssemblySha256=$hash;exitCode=$rc;completedUtc=[DateTimeOffset]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $out 'native-test-result.json') -Encoding utf8
Get-Content -LiteralPath (Join-Path $out 'native-full.log') -Tail 14
exit $rc
