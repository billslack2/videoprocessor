$ErrorActionPreference='Stop'
$sourceRoot='E:\codex\videoprocessor\vp-0218-local-integration-20261027'
$out='E:\codex\diagnostics\vp-0218-local-integration-20261027'
$expected='f4219a7aa679c57f1f6d637cb08406b9c2b4b97b'
if((& git -C $sourceRoot rev-parse HEAD).Trim() -ne $expected -or @(& git -C $sourceRoot status --porcelain).Count){throw 'Source must remain clean and pinned'}
$env:PSModulePath="$env:ProgramFiles\WindowsPowerShell\Modules;$env:windir\System32\WindowsPowerShell\v1.0\Modules"
& 'C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\Launch-VsDevShell.ps1' -VsInstallationPath 'C:\Program Files\Microsoft Visual Studio\18\Professional' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation -NoLogo
Set-Location -LiteralPath $sourceRoot
$started=[DateTimeOffset]::UtcNow.ToString('o')
[ordered]@{sourceCommit=$expected;localBase='f22e22c4f0dcd9be87c26189fc18a83a798c9a26';beta='7394ba420999a8fa708718ee4ce4d87e7459369f';configuration='Release';architecture='x64';startedUtc=$started;command='MSBuild.exe VideoProcessor.sln /t:Build /m /p:Configuration=Release /p:Platform=x64 /p:MagewellSdkRoot=E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3 /v:minimal'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $out 'build-invocation.json') -Encoding utf8
& MSBuild.exe VideoProcessor.sln /t:Build /m /p:Configuration=Release /p:Platform=x64 /p:MagewellSdkRoot=E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3 /v:minimal *> (Join-Path $out 'release-build.log')
$buildExit=$LASTEXITCODE
if((& git rev-parse HEAD).Trim() -ne $expected -or @(& git status --porcelain).Count){$buildExit=99}
[ordered]@{sourceCommit=$expected;configuration='Release';architecture='x64';exitCode=$buildExit;startedUtc=$started;completedUtc=[DateTimeOffset]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $out 'build-result.json') -Encoding utf8
Get-Content -LiteralPath (Join-Path $out 'release-build.log') -Tail 18
exit $buildExit
