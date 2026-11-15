[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$PortableRoot,
 [Parameter(Mandatory=$true)][string]$PayloadRoot,
 [Parameter(Mandatory=$true)][string]$IsccPath
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'installer_test_helpers.ps1')
$PortableRoot=(Resolve-Path -LiteralPath $PortableRoot).Path
$PayloadRoot=(Resolve-Path -LiteralPath $PayloadRoot).Path
$IsccPath=(Resolve-Path -LiteralPath $IsccPath).Path
$testRoot=Join-Path $root ('artifacts\za-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$null=New-Item -ItemType Directory -Path $testRoot -Force
$fixture=& (Join-Path $PSScriptRoot 'prepare_installer_fixture.ps1') -PayloadRoot $PayloadRoot -OutputDirectory (Join-Path $testRoot 'installer') -IsccPath $IsccPath -Flavor full
$payload=Get-Content -LiteralPath $fixture.ManifestPath -Raw | ConvertFrom-Json
$registry='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{'+$fixture.ProductGuid+'}_is1'
if(Test-VpTestRegistration $fixture.ProductGuid){throw 'An existing isolated fixture registration is present; refusing to touch it.'}
$targets=@()
$script:checks=0
function Assert([bool]$Condition,[string]$Message){
 if(-not $Condition){throw $Message};$script:checks++;Write-Host "PASS $Message"
}
try {
 foreach($preserveExtras in @($false,$true)) {
  $target=Join-Path $env:TEMP ('vpza-'+[guid]::NewGuid().ToString('N').Substring(0,8))
  $targets+=,$target
  $null=New-Item -ItemType Directory -Path $target -Force
  Get-ChildItem -LiteralPath $PortableRoot | Copy-Item -Destination $target -Recurse
  Copy-Item -LiteralPath (Join-Path $target 'VideoProcessor.cfg.example') -Destination (Join-Path $target 'VideoProcessor.cfg')
  $oldManifest=Get-Content -LiteralPath (Join-Path $target 'INSTALL-MANIFEST.json') -Raw | ConvertFrom-Json
  $oldManifest.applicationId=$fixture.ApplicationId
  $oldManifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $target 'INSTALL-MANIFEST.json') -Encoding UTF8
  Add-Content -LiteralPath (Join-Path $target 'VideoProcessor.cfg') -Value '# operator comment preserved by ZIP adoption'
  $sentinels=@{'VideoProcessor.state'='operator state';'config\profile-user.json'='operator profile';'shaders\private.hlsl'='private shader';'luts\private.cube'='private LUT';'logs\operator.log'='operator log'}
  if($preserveExtras){
   $sentinels['docs\VP-0174-config-ui.md']='operator edited this development note'
   $sentinels['prerequisites\personal.bak']='user backup, never installer owned'
   $sentinels['setup\private.txt']='unowned setup-directory note'
  }
  foreach($relative in $sentinels.Keys){
   $file=Join-Path $target $relative
   $null=New-Item -ItemType Directory -Path (Split-Path -Parent $file) -Force
   [IO.File]::WriteAllText($file,$sentinels[$relative])
  }
  $before=@{}
  foreach($relative in @('VideoProcessor.cfg')+@($sentinels.Keys)){
   $before[$relative]=(Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash
  }
  [IO.File]::WriteAllText((Join-Path $target 'config\obsolete-owned.dll'),'old owned binary')
  $releasePath=Join-Path $target 'RELEASE-MANIFEST.json'
  $release=Get-Content -LiteralPath $releasePath -Raw | ConvertFrom-Json
  $release.files+= [pscustomobject]@{destination='config/obsolete-owned.dll'}
  $release | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $releasePath -Encoding UTF8
  $process=Start-Process -FilePath $fixture.InstallerPath -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/DIR="'+$target+'"'),('/LOG="'+(Join-Path $testRoot ('adopt-'+$preserveExtras+'.log'))+'"')) -WindowStyle Hidden -Wait -PassThru
  if($process.ExitCode -ne 0){throw "ZIP adoption failed: $($process.ExitCode); see $testRoot"}
  Assert (-not (Test-Path -LiteralPath (Join-Path $target 'config\obsolete-owned.dll'))) 'ZIP inventory retires obsolete owned DLL'
  foreach($relative in $before.Keys){
   Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "ZIP adoption preserves $relative"
  }
  foreach($entry in $payload.cleanupFiles){
   $relative=$entry.path.Replace('/','\')
   if($sentinels.ContainsKey($relative)){continue}
   if(Test-Path -LiteralPath (Join-Path $target $relative)){throw "Unnecessary ZIP artifact retained: $relative"}
  }
  foreach($directory in @('.vp-installer-backups')+$(if(-not $preserveExtras){@('prerequisites','setup')}else{@()})){
   if(Test-Path -LiteralPath (Join-Path $target $directory)){throw "Unnecessary directory retained: $directory"}
  }
  $installed=Get-Content -LiteralPath (Join-Path $target 'INSTALL-MANIFEST.json') -Raw | ConvertFrom-Json
  foreach($entry in $installed.files){
   if($entry.policy -eq 'managed' -and (Get-FileHash -LiteralPath (Join-Path $target $entry.path)).Hash -ne $entry.sha256){
    throw "Mixed installed payload: $($entry.path)"
   }
  }
  $uninstaller=Join-Path $target 'unins000.exe'
  $process=Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait -PassThru
  if($process.ExitCode -ne 0 -or (Test-VpTestRegistration $fixture.ProductGuid)){throw 'ZIP adoption test cleanup failed'}
  Write-Host "PASS actual ZIP adoption with edited/user extras=$preserveExtras and isolated registration."
 }
}
finally {
 if(Test-VpTestRegistration $fixture.ProductGuid){
  $hive=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
  try {
   $key=$hive.OpenSubKey(('Software\Microsoft\Windows\CurrentVersion\Uninstall\{'+$fixture.ProductGuid+'}_is1'),$false)
   if(-not $key){throw 'Fixture registration disappeared during cleanup.'}
   $registeredPath=[string]$key.GetValue('Inno Setup: App Path')
   $uninstall=[string]$key.GetValue('UninstallString')
   $key.Dispose()
  } finally {$hive.Dispose()}
  $registeredPath=[IO.Path]::GetFullPath($registeredPath.TrimEnd('\'))
  $knownTarget=$false
  foreach($target in $targets){if([IO.Path]::GetFullPath($target).Equals($registeredPath,[StringComparison]::OrdinalIgnoreCase)){$knownTarget=$true}}
  if(-not $knownTarget){throw "Refusing to clean fixture registration outside this run's targets: $registeredPath"}
  $uninstallPath=$uninstall.Trim('"')
  if(-not (Test-Path -LiteralPath $uninstallPath)){throw "Fixture registration remains but its uninstaller is missing; preserving $registeredPath and $testRoot"}
  $process=Start-Process -FilePath $uninstallPath -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait -PassThru
  if($process.ExitCode -ne 0 -or (Test-VpTestRegistration $fixture.ProductGuid)){throw "Fixture uninstall did not clear its registration; preserving $registeredPath and $testRoot"}
 }
 foreach($target in $targets){
  $safe=[IO.Path]::GetFullPath($target)
  $temp=[IO.Path]::GetFullPath($env:TEMP).TrimEnd('\')+'\'
  if($safe.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($safe) -match '^vpza-[0-9a-f]{8}$' -and (Test-Path -LiteralPath $safe)){
   Remove-Item -LiteralPath $safe -Recurse -Force
  }
 }
}
Write-Host "$script:checks ZIP adoption assertions passed. Evidence: $testRoot"
