[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$IsccPath)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'installer_test_helpers.ps1')
$IsccPath=(Resolve-Path -LiteralPath $IsccPath).Path
$testRoot=Join-Path $root ('artifacts\il-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$null=New-Item -ItemType Directory -Path $testRoot -Force
$installRoot=Join-Path $env:TEMP ('vpil-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$fixtureGuid='0B37C49A-79D2-4D5D-9A7E-C99B31A2F001'
$registry='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{'+$fixtureGuid+'}_is1'
if(Test-VpTestRegistration $fixtureGuid){throw 'An existing isolated fixture registration is present; refusing to touch it.'}
$displayLabel='VideoProcessor QA Lifecycle '+[guid]::NewGuid().ToString('N').Substring(0,8)
$script:checks=0
function Assert([bool]$Condition,[string]$Message){
 if(-not $Condition){throw $Message};$script:checks++;Write-Host "PASS $Message"
}
function New-Payload([string]$Name,[string]$HostBytes,[string]$RendererBytes,[string]$ConfigBytes,[string]$SeedText) {
 $payload=Join-Path $testRoot $Name
 $null=New-Item -ItemType Directory -Path (Join-Path $payload 'vprenderer'),(Join-Path $payload 'config') -Force
 $data=@{
  'VideoProcessor.exe'=$HostBytes
  'vprenderer\VideoProcessorVPRenderer.dll'=$RendererBytes
  'config\VideoProcessorConfig.exe'=$ConfigBytes
  'VideoProcessor.cfg'=$SeedText
 }
 $entries=@()
 foreach($relative in $data.Keys){
  $file=Join-Path $payload $relative
  $null=New-Item -ItemType Directory -Path (Split-Path -Parent $file) -Force
  [IO.File]::WriteAllText($file,$data[$relative],[Text.Encoding]::ASCII)
  $policy=if($relative -eq 'VideoProcessor.cfg'){'seed'}else{'managed'}
  $entries+= [ordered]@{path=$relative.Replace('\','/');policy=$policy;sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToUpperInvariant()}
 }
 $manifest=[ordered]@{
  schemaVersion=1
  applicationId='VideoProcessor-42D852F1-70E9-43ED-8739-D61752106D59'
  flavor='full'
  coreVersion='1.3.005-beta'
  build=$Name
  files=$entries
  cleanupFiles=@()
  uninstallEntryPoint='Uninstall VideoProcessor.lnk'
 }
 $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $payload 'INSTALL-MANIFEST.json') -Encoding UTF8
 return $payload
}
function New-Fixture([string]$Payload,[string]$Name,[switch]$NoCompile) {
 $out=Join-Path $testRoot $Name
 $arguments=@{PayloadRoot=$Payload;OutputDirectory=$out;IsccPath=$IsccPath;Flavor='full';DisplayLabel=$displayLabel}
 if($NoCompile){$arguments.NoCompile=$true}
 return & (Join-Path $PSScriptRoot 'prepare_installer_fixture.ps1') @arguments
}
function Run-Setup([string]$Installer,[string]$Name,[switch]$SelectDirectory) {
 $arguments=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/LOG="'+(Join-Path $testRoot ($Name+'.log'))+'"'))
 if($SelectDirectory){$arguments+=('/DIR="'+$installRoot+'"')}
 return Start-Process -FilePath $Installer -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
}
function Assert-Managed([string]$ManifestPath,[string]$ExpectedHost,[string]$ExpectedRenderer) {
 $manifest=Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
 foreach($entry in $manifest.files){
  if($entry.policy -eq 'managed'){
   $actual=(Get-FileHash -LiteralPath (Join-Path $installRoot $entry.path)).Hash
   Assert ($actual -eq $entry.sha256) "installed managed hash matches $($entry.path)"
  }
 }
 Assert ([IO.File]::ReadAllText((Join-Path $installRoot 'VideoProcessor.exe')) -eq $ExpectedHost) 'selected host bytes are installed'
 Assert ([IO.File]::ReadAllText((Join-Path $installRoot 'vprenderer\VideoProcessorVPRenderer.dll')) -eq $ExpectedRenderer) 'selected renderer bytes are installed'
}
$aPayload=New-Payload 'A' 'host-A' 'renderer-A' 'config-A' '# source cfg A'
$bPayload=New-Payload 'B' 'host-B' 'renderer-B' 'config-B' '# source cfg B'
$a=New-Fixture $aPayload 'setup-A'
$b=New-Fixture $bPayload 'setup-B'
$corrupt=New-Fixture $bPayload 'setup-corrupt' -NoCompile
$bad=Get-Content -LiteralPath $corrupt.ManifestPath -Raw | ConvertFrom-Json
($bad.files | Where-Object path -eq 'VideoProcessor.exe').sha256='0'*64
$bad | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $corrupt.ManifestPath -Encoding UTF8
$numbers=($bad.coreVersion -split '-',2)[0].Split('.')
$fileVersion=($numbers -join '.')+'.0'
$badBase='DO-NOT-DISTRIBUTE-fixture-failure'
& $IsccPath "/DPayloadRoot=$bPayload" "/DPayloadInclude=$($corrupt.PayloadIncludePath)" "/DOutputRoot=$($corrupt.OutputDirectory)" "/DCoreVersion=$($bad.coreVersion)" '/DBuildCommit=fixture' "/DInstallerBaseName=$badBase" "/DFileVersion=$fileVersion" "/DSetupIcon=$root\images\VideoProcessor.ico" (Join-Path $corrupt.OutputDirectory 'fixture.iss') *> (Join-Path $testRoot 'failure-compile.log')
if($LASTEXITCODE -ne 0){throw 'Could not compile the intentionally invalid fixture installer.'}
$badInstaller=Join-Path $corrupt.OutputDirectory ($badBase+'.exe')
try {
 $process=Run-Setup $a.InstallerPath 'fresh-A' -SelectDirectory
 Assert ($process.ExitCode -eq 0) 'actual fixture installer A completes'
 Assert ((Get-ItemProperty -LiteralPath $registry).'Inno Setup: App Path' -ieq $installRoot) 'isolated registration records the custom install path'
 Assert-Managed $a.ManifestPath 'host-A' 'renderer-A'
 Assert (-not (Test-Path -LiteralPath (Join-Path $installRoot '.vp-installer-backups'))) 'successful setup prunes its recovery journal'
 Assert (-not (Test-Path -LiteralPath (Join-Path $installRoot 'setup'))) 'setup helper is not installed in the application folder'
 [IO.File]::WriteAllText((Join-Path $installRoot 'VideoProcessor.cfg'),("# operator comments kept"+[Environment]::NewLine+"unknown.key = custom"))
 [IO.File]::WriteAllText((Join-Path $installRoot 'VideoProcessor.state'),'operator state bytes')
 $cfgBefore=(Get-FileHash (Join-Path $installRoot 'VideoProcessor.cfg')).Hash
 $stateBefore=(Get-FileHash (Join-Path $installRoot 'VideoProcessor.state')).Hash
 $process=Run-Setup $b.InstallerPath 'upgrade-B'
 Assert ($process.ExitCode -eq 0) 'actual fixture installer updates A to B'
 Assert-Managed $b.ManifestPath 'host-B' 'renderer-B'
 Assert ((Get-FileHash (Join-Path $installRoot 'VideoProcessor.cfg')).Hash -eq $cfgBefore) 'A-to-B preserves edited config bytes and comments'
 Assert ((Get-FileHash (Join-Path $installRoot 'VideoProcessor.state')).Hash -eq $stateBefore) 'A-to-B preserves user state bytes'
 $process=Run-Setup $badInstaller 'corrupt-B'
 Assert ($process.ExitCode -ne 0) 'corrupt managed hash makes actual setup fail'
 Assert-Managed $b.ManifestPath 'host-B' 'renderer-B'
 Assert ((Get-FileHash (Join-Path $installRoot 'VideoProcessor.cfg')).Hash -eq $cfgBefore) 'failed setup preserves edited config bytes'
 Assert ((Get-FileHash (Join-Path $installRoot 'VideoProcessor.state')).Hash -eq $stateBefore) 'failed setup preserves user state bytes'
 Assert (Test-VpTestRegistration $fixtureGuid) 'failed update retains valid fixture registration'
 $uninstaller=Join-Path $installRoot 'unins000.exe'
 $process=Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait -PassThru
 Assert ($process.ExitCode -eq 0 -and -not (Test-VpTestRegistration $fixtureGuid)) 'actual fixture uninstall removes only its isolated registration'
 Assert ((Get-FileHash (Join-Path $installRoot 'VideoProcessor.cfg')).Hash -eq $cfgBefore) 'uninstall preserves edited config bytes'
 Assert ((Get-FileHash (Join-Path $installRoot 'VideoProcessor.state')).Hash -eq $stateBefore) 'uninstall preserves user state bytes'
 $process=Run-Setup $b.InstallerPath 'reinstall-B' -SelectDirectory
 Assert ($process.ExitCode -eq 0) 'actual fixture reinstall succeeds after uninstall'
 Assert ((Get-FileHash (Join-Path $installRoot 'VideoProcessor.cfg')).Hash -eq $cfgBefore) 'reinstall preserves existing config bytes'
 Assert ((Get-FileHash (Join-Path $installRoot 'VideoProcessor.state')).Hash -eq $stateBefore) 'reinstall preserves existing state bytes'
 $uninstaller=Join-Path $installRoot 'unins000.exe'
 $process=Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait -PassThru
 Assert ($process.ExitCode -eq 0 -and -not (Test-VpTestRegistration $fixtureGuid)) 'fixture registration removed after final uninstall'
}
finally {
 $safe=[IO.Path]::GetFullPath($installRoot)
 $temp=[IO.Path]::GetFullPath($env:TEMP).TrimEnd('\')+'\'
 if(-not $safe.StartsWith($temp,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetFileName($safe) -notmatch '^vpil-[0-9a-f]{8}$'){
  throw 'Unsafe lifecycle fixture cleanup path; preserving test output.'
 }
 if(Test-VpTestRegistration $fixtureGuid){
  $hive=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
  try {
   $key=$hive.OpenSubKey(('Software\Microsoft\Windows\CurrentVersion\Uninstall\{'+$fixtureGuid+'}_is1'),$false)
   if(-not $key){throw 'Fixture registration disappeared during cleanup.'}
   $registeredName=[string]$key.GetValue('DisplayName')
   $registeredPath=[string]$key.GetValue('Inno Setup: App Path')
   $uninstall=[string]$key.GetValue('UninstallString')
   $key.Dispose()
  } finally {$hive.Dispose()}
  if($registeredName -cne $displayLabel -or [IO.Path]::GetFullPath($registeredPath.TrimEnd('\')) -ine $safe){
   throw "Refusing cleanup of registration outside this lifecycle fixture: $registeredName at $registeredPath"
  }
  $uninstallPath=$uninstall.Trim('"')
  if(-not (Test-Path -LiteralPath $uninstallPath)){throw "Fixture registration remains but its uninstaller is missing; preserving $safe and $testRoot"}
  $process=Start-Process -FilePath $uninstallPath -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait -PassThru
  if($process.ExitCode -ne 0 -or (Test-VpTestRegistration $fixtureGuid)){throw "Fixture uninstall did not clear its registration; preserving $safe and $testRoot"}
 }
 if(Test-Path -LiteralPath $safe){
  Remove-Item -LiteralPath $safe -Recurse -Force
 }
}
Write-Host "$script:checks actual Inno fixture lifecycle checks passed. Evidence: $testRoot"

