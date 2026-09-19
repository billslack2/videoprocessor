[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$InstallerPath,
    [Parameter(Mandatory=$true)][string]$PayloadRoot,
    [string]$PreviousInstallerPath, [string]$PreviousManifestPath
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
$registry='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{42D852F1-70E9-43ED-8739-D61752106D59}_is1'
if(Test-Path $registry){throw 'An existing installer registration is present. Use a disposable Windows account for these tests.'}
$InstallerPath=(Resolve-Path -LiteralPath $InstallerPath).Path
$PayloadRoot=(Resolve-Path -LiteralPath $PayloadRoot).Path
$testRoot=Join-Path $root ('artifacts\installer-e2e-'+[guid]::NewGuid().ToString('N'))
$target=Join-Path $testRoot 'Custom VP space'
$null=New-Item -ItemType Directory -Path $target -Force
$script:checks=0
$script:ActiveInstaller=$InstallerPath
$script:ExpectedManifest=Join-Path $PayloadRoot 'INSTALL-MANIFEST.json'
if ($PreviousInstallerPath -or $PreviousManifestPath) {
 if (-not $PreviousInstallerPath -or -not $PreviousManifestPath) { throw 'Supply both previous build inputs.' }
 $PreviousInstallerPath=(Resolve-Path -LiteralPath $PreviousInstallerPath).Path
 $PreviousManifestPath=(Resolve-Path -LiteralPath $PreviousManifestPath).Path
 $a=Get-Content $PreviousManifestPath -Raw | ConvertFrom-Json
 $b=Get-Content $script:ExpectedManifest -Raw | ConvertFrom-Json
 if ($a.build -eq $b.build -or $a.coreVersion -ne $b.coreVersion) { throw 'A and B must be distinct commits with the same core version.' }
 $script:ActiveInstaller=$PreviousInstallerPath
 $script:ExpectedManifest=$PreviousManifestPath
}
function Assert([bool]$Condition,[string]$Message){
 if(-not $Condition){throw $Message};$script:checks++;Write-Host "PASS $Message"
}
function RunSetup([string]$Label,[bool]$SelectDirectory){
 $args=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/LOG="'+(Join-Path $testRoot ($Label+'.log'))+'"'))
 if($SelectDirectory){$args+=('/DIR="'+$target+'"')}
 $process=Start-Process -FilePath $script:ActiveInstaller -ArgumentList $args -WindowStyle Hidden -Wait -PassThru
 Assert ($process.ExitCode -eq 0) "actual installer $Label exit zero"
 $installed=(Get-ItemProperty -LiteralPath $registry).'Inno Setup: App Path'
 Assert ($installed.TrimEnd('\') -ieq $target.TrimEnd('\')) "actual installer $Label uses original custom directory"
 $manifest=Get-Content -LiteralPath $script:ExpectedManifest -Raw | ConvertFrom-Json
 foreach($entry in $manifest.files){
  if($entry.policy -eq 'managed'){
   $hash=(Get-FileHash -LiteralPath (Join-Path $target $entry.path) -Algorithm SHA256).Hash
   if($hash -ne $entry.sha256){throw "Installed payload mismatch: $($entry.path)"}
  }
 }
 Assert $true "actual installer $Label managed hashes match, including host/renderer"
 if($manifest.PSObject.Properties['cleanupFiles']){
  foreach($entry in $manifest.cleanupFiles){
   Assert (-not (Test-Path -LiteralPath (Join-Path $target $entry.path))) "actual installer $Label excludes setup-only file $($entry.path)"
  }
  foreach($directory in @('prerequisites','setup','.vp-installer-backups')){
   Assert (-not (Test-Path -LiteralPath (Join-Path $target $directory))) "actual installer $Label leaves no $directory directory"
  }
  foreach($file in @('docs/CONFIGURATION.html','docs/VideoProcessor_NLS_Configuration_Guide.pdf','LICENSE.txt','vprenderer/third_party_licenses/libplacebo-LICENSE.txt')){
   Assert (Test-Path -LiteralPath (Join-Path $target $file)) "actual installer $Label retains user guide/license $file"
  }
 }
 $keys=@(Get-ChildItem 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' |
    Where-Object PSChildName -like '*42D852F1-70E9-43ED-8739-D61752106D59*')
 Assert ($keys.Count -eq 1) "actual installer $Label has one uninstall entry"
}
RunSetup 'fresh' $true
# Launch the real editor in background mode; do not touch or discard editor work.
$configProcess=Start-Process -FilePath (Join-Path $target 'config\VideoProcessorConfig.exe') -ArgumentList @('--background','--config',('"'+(Join-Path $target 'VideoProcessor.cfg')+'"')) -WindowStyle Hidden -PassThru
try {
 Start-Sleep -Seconds 2
 Assert (-not $configProcess.HasExited) 'packaged Config launches without elevation'
 $beforeBlocked=(Get-FileHash -LiteralPath (Join-Path $target 'VideoProcessor.exe')).Hash
 $blocked=Start-Process -FilePath $script:ActiveInstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/LOG="'+(Join-Path $testRoot 'running-config.log')+'"')) -WindowStyle Hidden -Wait -PassThru
 Assert ($blocked.ExitCode -ne 0) 'actual installer refuses running background Config'
 Assert ((Get-FileHash -LiteralPath (Join-Path $target 'VideoProcessor.exe')).Hash -eq $beforeBlocked) 'running-process rejection preserves old application'
} finally {
 # This exact process was started by this test and has no edited document.
 if (-not $configProcess.HasExited) { Stop-Process -Id $configProcess.Id; $configProcess.WaitForExit() }
}
$sentinels=@{
 'VideoProcessor.cfg'="# Operator configuration with comments preserved";
 'VideoProcessor.state'='operator-state';
 'shaders\NLS.glsl'='operator-modified shipped shader';
 'shaders\private.hlsl'='private shader';
 'luts\private.cube'='private LUT';
 'logs\operator.log'='private log';
 'private.txt'='unowned file'
}
$before=@{}
foreach($relative in $sentinels.Keys){
 $file=Join-Path $target $relative
 $null=New-Item -ItemType Directory -Path (Split-Path -Parent $file) -Force
 [IO.File]::WriteAllText($file,$sentinels[$relative])
 $before[$relative]=(Get-FileHash -LiteralPath $file).Hash
}
if ($PreviousInstallerPath) {
 $script:ActiveInstaller=$InstallerPath
 $script:ExpectedManifest=Join-Path $PayloadRoot 'INSTALL-MANIFEST.json'
 RunSetup 'upgrade-same-core-version' $false
 foreach($relative in $before.Keys){
  Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "same-core upgrade preserves $relative"
 }
}
# Corrupt owned files to prove equal-version reinstall replaces actual bytes.
[IO.File]::WriteAllText((Join-Path $target 'VideoProcessor.exe'),'stale managed payload')
[IO.File]::WriteAllText((Join-Path $target 'vprenderer\VideoProcessorVPRenderer.dll'),'stale renderer')
RunSetup 'reinstall-remembered-path' $false
foreach($relative in $before.Keys){
 Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "actual reinstall preserves $relative"
}
if ($PreviousInstallerPath) {
 $script:ActiveInstaller=$PreviousInstallerPath
 $script:ExpectedManifest=$PreviousManifestPath
 RunSetup 'rollback-older-commit' $false
 foreach($relative in $before.Keys){
  Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "rollback preserves $relative"
 }
 $script:ActiveInstaller=$InstallerPath
 $script:ExpectedManifest=Join-Path $PayloadRoot 'INSTALL-MANIFEST.json'
}
$uninstaller=Join-Path $target 'unins000.exe'
$process=Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+(Join-Path $testRoot 'uninstall.log')+'"')) -WindowStyle Hidden -Wait -PassThru
Assert ($process.ExitCode -eq 0) 'actual uninstall exit zero'
Assert (-not (Test-Path $registry)) 'actual uninstall removes registration'
Assert (-not (Test-Path -LiteralPath (Join-Path $target 'VideoProcessor.exe'))) 'actual uninstall removes managed host'
foreach($relative in $before.Keys){
 Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "actual uninstall preserves $relative"
}
RunSetup 'reinstall-preserved-directory' $true
foreach($relative in $before.Keys){
 Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "reinstall after uninstall preserves $relative"
}
$process=Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait -PassThru
Assert ($process.ExitCode -eq 0 -and -not (Test-Path $registry)) 'test installation registration cleaned up'
Write-Host "$checks actual-installer checks passed. Logs and retained data: $testRoot"
