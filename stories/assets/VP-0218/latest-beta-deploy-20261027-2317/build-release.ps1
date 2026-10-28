$ErrorActionPreference='Stop'
$sourceRoot='E:\codex\videoprocessor\vp-0218-latest-20261027-2307'
$out='E:\codex\diagnostics\vp-0218-latest-20261027-2307'
$expected='13878270b559339f82896345c66b113b56dfd39e'
if((& git -C $sourceRoot rev-parse HEAD).Trim() -ne $expected -or @(& git -C $sourceRoot status --porcelain).Count){throw 'Source must remain clean and pinned'}
$env:PSModulePath="$env:ProgramFiles\WindowsPowerShell\Modules;$env:windir\System32\WindowsPowerShell\v1.0\Modules"
& 'C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\Launch-VsDevShell.ps1' -VsInstallationPath 'C:\Program Files\Microsoft Visual Studio\18\Professional' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation -NoLogo
Set-Location -LiteralPath $sourceRoot
$started=[DateTimeOffset]::UtcNow.ToString('o')
[ordered]@{sourceCommit=$expected;localBase='3bae9f228173b9b2d024d7c977a87766bcbc02c1';beta='3bae9f228173b9b2d024d7c977a87766bcbc02c1';configuration='Release';architecture='x64';startedUtc=$started;command='MSBuild.exe VideoProcessor.sln /t:Build /m /p:Configuration=Release /p:Platform=x64 /p:MagewellSdkRoot=E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3 /v:minimal'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $out 'build-invocation.json') -Encoding utf8
& MSBuild.exe VideoProcessor.sln /t:Build /m /p:Configuration=Release /p:Platform=x64 /p:MagewellSdkRoot=E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3 /v:minimal *> (Join-Path $out 'release-build.log')
$buildExit=$LASTEXITCODE
if((& git rev-parse HEAD).Trim() -ne $expected -or @(& git status --porcelain).Count){$buildExit=99}
[ordered]@{sourceCommit=$expected;configuration='Release';architecture='x64';exitCode=$buildExit;startedUtc=$started;completedUtc=[DateTimeOffset]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $out 'build-result.json') -Encoding utf8
Get-Content -LiteralPath (Join-Path $out 'release-build.log') -Tail 18
exit $buildExit

