[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PayloadRoot,
    [Parameter(Mandatory=$true)][string]$IsccPath,
    [string]$PreviousPayloadRoot, [string]$PreviousManifestPath
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'installer_test_helpers.ps1')
. (Join-Path $PSScriptRoot 'installer_build_identity.ps1')
$PayloadRoot=(Resolve-Path -LiteralPath $PayloadRoot).Path
$IsccPath=(Resolve-Path -LiteralPath $IsccPath).Path
$testRoot=Join-Path $root ('artifacts\e2e-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$null=New-Item -ItemType Directory -Path $testRoot -Force
$target=Join-Path $env:TEMP ('vpe2e-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$displayLabel='VideoProcessor QA E2E '+[guid]::NewGuid().ToString('N').Substring(0,8)
$fixture=& (Join-Path $PSScriptRoot 'prepare_installer_fixture.ps1') -PayloadRoot $PayloadRoot -OutputDirectory (Join-Path $testRoot 'B') -IsccPath $IsccPath -Flavor full -DisplayLabel $displayLabel
$script:LatestInstaller=$fixture.InstallerPath
$script:ActiveInstaller=$script:LatestInstaller
$script:ExpectedManifest=$fixture.ManifestPath
$script:PreviousInstaller=$null
$registry='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{0B37C49A-79D2-4D5D-9A7E-C99B31A2F001}_is1'
if(Test-VpTestRegistration '0B37C49A-79D2-4D5D-9A7E-C99B31A2F001'){throw 'An existing isolated fixture registration is present; refusing to touch it.'}
if ($PreviousPayloadRoot -or $PreviousManifestPath) {
 if (-not $PreviousPayloadRoot -or -not $PreviousManifestPath) { throw 'Supply both previous payload root and previous manifest.' }
 $PreviousPayloadRoot=(Resolve-Path -LiteralPath $PreviousPayloadRoot).Path
 $PreviousManifestPath=(Resolve-Path -LiteralPath $PreviousManifestPath).Path
 $a=Get-Content $PreviousManifestPath -Raw | ConvertFrom-Json
 $b=Get-Content (Join-Path $PayloadRoot 'INSTALL-MANIFEST.json') -Raw | ConvertFrom-Json
 if ($a.build -eq $b.build -or $a.coreVersion -ne $b.coreVersion) { throw 'A and B must be distinct commits with the same core version.' }
 $previousFixture=& (Join-Path $PSScriptRoot 'prepare_installer_fixture.ps1') -PayloadRoot $PreviousPayloadRoot -OutputDirectory (Join-Path $testRoot 'A') -IsccPath $IsccPath -Flavor full -DisplayLabel $displayLabel
 $script:PreviousInstaller=$previousFixture.InstallerPath
 $script:ActiveInstaller=$script:PreviousInstaller
 $script:ExpectedManifest=$previousFixture.ManifestPath
}
$script:checks=0
function Assert([bool]$Condition,[string]$Message){
 if(-not $Condition){throw $Message};$script:checks++;Write-Host "PASS $Message"
}
function Get-Uninstaller {
 ((Get-ItemProperty -LiteralPath $registry).UninstallString).Trim('"')
}
$desktopPaths=@([Environment]::GetFolderPath('DesktopDirectory'),[Environment]::GetFolderPath('CommonDesktopDirectory')) | Select-Object -Unique
$desktopBefore=@{}
foreach($desktop in $desktopPaths){
 foreach($file in Get-ChildItem -LiteralPath $desktop -File -ErrorAction SilentlyContinue){
  $desktopBefore[$file.FullName]=(Get-FileHash -LiteralPath $file.FullName).Hash
 }
}
function AssertDesktop {
 foreach($desktop in $desktopPaths){
  foreach($file in Get-ChildItem -LiteralPath $desktop -File -ErrorAction SilentlyContinue){
   if(-not $desktopBefore.ContainsKey($file.FullName) -or $desktopBefore[$file.FullName] -ne (Get-FileHash -LiteralPath $file.FullName).Hash){throw "Desktop changed: $($file.FullName)"}
  }
 }
 Assert $true 'installer creates no desktop files'
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
 if($manifest.PSObject.Properties['uninstallEntryPoint']){
  $shortcut=Join-Path $target $manifest.uninstallEntryPoint
  Assert (Test-Path -LiteralPath $shortcut) "actual installer $Label provides Uninstall VideoProcessor shortcut"
  $link=(New-Object -ComObject WScript.Shell).CreateShortcut($shortcut)
  Assert ($link.TargetPath -ieq (Get-Uninstaller)) "actual installer $Label shortcut targets registered uninstaller"
  foreach($support in @((Get-Uninstaller),[IO.Path]::ChangeExtension((Get-Uninstaller),'.dat'))){
   Assert ([bool]((Get-Item -LiteralPath $support -Force).Attributes -band [IO.FileAttributes]::Hidden)) "actual installer $Label hides internal $support"
  }
 }
 AssertDesktop
 $keys=@(Get-ChildItem 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' |
    Where-Object PSChildName -like '*0B37C49A-79D2-4D5D-9A7E-C99B31A2F001*')
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
if ($PreviousPayloadRoot) {
 $script:ActiveInstaller=$script:LatestInstaller
 $script:ExpectedManifest=$fixture.ManifestPath
 RunSetup 'upgrade-same-core-version' $false
 foreach($relative in $before.Keys){
  Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "same-core upgrade preserves $relative"
 }
}
$current=Get-Content -LiteralPath $script:ExpectedManifest -Raw | ConvertFrom-Json
if($current.PSObject.Properties['uninstallEntryPoint']){
 $configProcess=Start-Process -FilePath (Join-Path $target 'config\VideoProcessorConfig.exe') -ArgumentList @('--background','--config',('"'+(Join-Path $target 'VideoProcessor.cfg')+'"')) -WindowStyle Hidden -PassThru
 try {
  Start-Sleep -Seconds 2
  Assert (-not $configProcess.HasExited) 'Config runs for the uninstall refusal test'
  if($current.PSObject.Properties['runtimeMode'] -and $current.runtimeMode -eq 'app-local'){
   $modules=@((Get-Process -Id $configProcess.Id).Modules | Where-Object ModuleName -match '^(msvcp140.*|vcruntime140.*|concrt140|mfc140u)\.dll$')
   Assert ($modules.Count -ge 3) 'Config loads its private VC runtime dependencies'
   foreach($module in $modules){
    Assert ($module.FileName.StartsWith((Join-Path $target 'config')+'\',[StringComparison]::OrdinalIgnoreCase)) "Config loads $($module.ModuleName) from its own folder"
   }
  }
  $uninstallLog=Join-Path $testRoot 'uninstall-running-config.log'
  $blocked=Start-Process -FilePath (Get-Uninstaller) -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+$uninstallLog+'"')) -WindowStyle Hidden -Wait -PassThru
  Assert ($blocked.ExitCode -ne 0) 'uninstall cancels safely while Config is in the tray'
  Assert (Test-Path -LiteralPath $registry) 'cancelled uninstall retains registration'
  $message=Get-Content -LiteralPath $uninstallLog -Raw
  Assert ($message -match 'VideoProcessor Config' -and $message -match 'only hides it' -and $message -match 'select Exit' -and $message -match 'Click Retry' -and $message -match 'Cancel') 'uninstall names Config and explains tray Exit, Retry and Cancel'
  foreach($relative in $before.Keys){
   Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "cancelled uninstall preserves $relative"
  }
 } finally {
  if(-not $configProcess.HasExited){Stop-Process -Id $configProcess.Id;$configProcess.WaitForExit()}
 }
}
# Corrupt owned files to prove equal-version reinstall replaces actual bytes.
[IO.File]::WriteAllText((Join-Path $target 'VideoProcessor.exe'),'stale managed payload')
[IO.File]::WriteAllText((Join-Path $target 'vprenderer\VideoProcessorVPRenderer.dll'),'stale renderer')
if($current.PSObject.Properties['runtimeMode'] -and $current.runtimeMode -eq 'app-local'){
 [IO.File]::WriteAllText((Join-Path $target 'config\msvcp140.dll'),'corrupt CRT')
 Remove-Item -LiteralPath (Join-Path $target 'vcruntime140.dll') -Force
 [IO.File]::WriteAllText((Join-Path $target 'INSTALL-MANIFEST.json'),'broken manifest')
 [IO.File]::WriteAllText((Join-Path $target 'config\obsolete-private.dll'),'unrecognized old binary')
}
RunSetup 'reinstall-remembered-path' $false
if($current.PSObject.Properties['runtimeMode'] -and $current.runtimeMode -eq 'app-local'){
 $saved=@(Get-ChildItem -LiteralPath (Join-Path $target 'recovered-files') -Recurse -File | Where-Object Name -eq 'obsolete-private.dll')
 Assert ($saved.Count -eq 1 -and [IO.File]::ReadAllText($saved[0].FullName) -eq 'unrecognized old binary') 'repair preserves unknown DLL outside active load path'
 Assert (-not (Test-Path -LiteralPath (Join-Path $target 'config\obsolete-private.dll'))) 'repair removes obsolete DLL from load path'
 Remove-Item -LiteralPath (Join-Path $target 'INSTALL-MANIFEST.json') -Force
 RunSetup 'missing-manifest' $false
 # Exercise a damaged native uninstall log; Inno may choose a new numbered pair.
 $damagedDat=[IO.File]::Open([IO.Path]::ChangeExtension((Get-Uninstaller),'.dat'),'Open','Write','None')
 try { $damagedDat.SetLength(0); $bytes=[Text.Encoding]::UTF8.GetBytes('damaged uninstall history'); $damagedDat.Write($bytes,0,$bytes.Length) }
 finally { $damagedDat.Dispose() }
 RunSetup 'damaged-uninstall-data' $false
}
foreach($relative in $before.Keys){
 Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "actual reinstall preserves $relative"
}
if ($PreviousPayloadRoot) {
 $script:ActiveInstaller=$script:PreviousInstaller
 $script:ExpectedManifest=$previousFixture.ManifestPath
 RunSetup 'rollback-older-commit' $false
 foreach($relative in $before.Keys){
  Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "rollback preserves $relative"
 }
 $script:ActiveInstaller=$script:LatestInstaller
 $script:ExpectedManifest=$fixture.ManifestPath
 RunSetup 'upgrade-after-rollback' $false
}
$uninstaller=Get-Uninstaller
$process=Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/LOG="'+(Join-Path $testRoot 'uninstall.log')+'"')) -WindowStyle Hidden -Wait -PassThru
Assert ($process.ExitCode -eq 0) 'actual uninstall exit zero'
Assert (-not (Test-VpTestRegistration '0B37C49A-79D2-4D5D-9A7E-C99B31A2F001')) 'actual uninstall removes registration'
Assert (-not (Test-Path -LiteralPath (Join-Path $target 'VideoProcessor.exe'))) 'actual uninstall removes managed host'
Assert (-not (Test-Path -LiteralPath (Join-Path $target 'Uninstall VideoProcessor.lnk'))) 'actual uninstall removes its friendly shortcut'
foreach($relative in $before.Keys){
 Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "actual uninstall preserves $relative"
}
RunSetup 'reinstall-preserved-directory' $true
foreach($relative in $before.Keys){
 Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "reinstall after uninstall preserves $relative"
}
# Only delete this disposable test folder. Retain all test operator data outside it.
$safeTarget=[IO.Path]::GetFullPath($target)
$tempRoot=[IO.Path]::GetFullPath($env:TEMP).TrimEnd('\')+'\'
if(-not $safeTarget.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -or
   [IO.Path]::GetFileName($safeTarget) -notmatch '^vpe2e-[0-9a-f]{8}$'){throw 'Unsafe deletion fixture'}
Copy-Item -LiteralPath $target -Destination (Join-Path $testRoot 'before-manual-deletion') -Recurse
Remove-Item -LiteralPath $safeTarget -Recurse -Force
Assert (Test-Path -LiteralPath $registry) 'manual folder deletion leaves remembered registration'
RunSetup 'repair-manually-deleted-folder' $false
Assert (Test-Path -LiteralPath (Join-Path $target 'VideoProcessor.cfg')) 'repair seeds configuration when no file survives'
$uninstaller=Get-Uninstaller
$process=Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait -PassThru
Assert ($process.ExitCode -eq 0 -and -not (Test-VpTestRegistration '0B37C49A-79D2-4D5D-9A7E-C99B31A2F001')) 'test installation registration cleaned up'
Write-Host "$checks actual-installer checks passed. Logs and retained data: $testRoot"
