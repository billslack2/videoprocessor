$ErrorActionPreference='Stop'
$sourceRoot='E:\codex\videoprocessor\vp-0218-local-integration-20261027';$out='E:\codex\diagnostics\vp-0218-local-integration-20261027'
$exe=Join-Path $sourceRoot 'x64\Release\VideoProcessorConfigTests.exe';$hash=(Get-FileHash -LiteralPath $exe).Hash
$env:QT_QPA_PLATFORM='offscreen';$env:QT_QPA_FONTDIR='C:\Windows\Fonts';$env:QT_PLUGIN_PATH=(Join-Path $sourceRoot 'x64\Release');$env:QT_SCALE_FACTOR='1';$env:VP_REQUIRE_CHART_RUNTIME='1';$env:PATH=(Join-Path $sourceRoot 'x64\Release')+';'+$env:PATH
& $exe --test 'subtitle background shape round trip and visibility' *> (Join-Path $out 'config-guard.log')
$rc=$LASTEXITCODE
[ordered]@{sourceCommit=(& git -C $sourceRoot rev-parse HEAD).Trim();testExecutableSha256=$hash;test='subtitle background shape round trip and visibility';exitCode=$rc;completedUtc=[DateTimeOffset]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $out 'config-test-result.json') -Encoding utf8
Get-Content -LiteralPath (Join-Path $out 'config-guard.log') -Tail 10
exit $rc
