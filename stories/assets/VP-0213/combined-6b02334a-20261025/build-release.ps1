$ErrorActionPreference='Stop'
$env:PSModulePath="$env:ProgramFiles\WindowsPowerShell\Modules;$env:windir\System32\WindowsPowerShell\v1.0\Modules"
& 'C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\Launch-VsDevShell.ps1' -VsInstallationPath 'C:\Program Files\Microsoft Visual Studio\18\Professional' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation -NoLogo
Set-Location E:\codex\videoprocessor\pr177-pr203-deploy-20261025
& MSBuild.exe VideoProcessor.sln /t:Build /m /p:Configuration=Release /p:Platform=x64 /p:MagewellSdkRoot=E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3 /v:minimal *> E:\codex\diagnostics\pr177-pr203-deploy-20261025\release-build.log
$vpExit=$LASTEXITCODE
Get-Content E:\codex\diagnostics\pr177-pr203-deploy-20261025\release-build.log -Tail 24
exit $vpExit

