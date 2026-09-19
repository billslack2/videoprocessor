[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$InstallerPath,
    [Parameter(Mandatory=$true)][string]$PayloadRoot
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
function Assert([bool]$Condition,[string]$Message){
 if(-not $Condition){throw $Message};$script:checks++;Write-Host "PASS $Message"
}
function RunSetup([string]$Label,[bool]$SelectDirectory){
 $args=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/LOG="'+(Join-Path $testRoot ($Label+'.log'))+'"'))
 if($SelectDirectory){$args+=('/DIR="'+$target+'"')}
 $process=Start-Process -FilePath $InstallerPath -ArgumentList $args -WindowStyle Hidden -Wait -PassThru
 Assert ($process.ExitCode -eq 0) "actual installer $Label exit zero"
 $installed=(Get-ItemProperty -LiteralPath $registry).'Inno Setup: App Path'
 Assert ($installed.TrimEnd('\') -ieq $target.TrimEnd('\')) "actual installer $Label uses original custom directory"
 $manifest=Get-Content -LiteralPath (Join-Path $PayloadRoot 'INSTALL-MANIFEST.json') -Raw | ConvertFrom-Json
 foreach($entry in $manifest.files){
  if($entry.policy -eq 'managed'){
   $hash=(Get-FileHash -LiteralPath (Join-Path $target $entry.path) -Algorithm SHA256).Hash
   if($hash -ne $entry.sha256){throw "Installed payload mismatch: $($entry.path)"}
  }
 }
 Assert $true "actual installer $Label managed hashes match, including host/renderer"
 $keys=@(Get-ChildItem 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' |
    Where-Object PSChildName -like '*42D852F1-70E9-43ED-8739-D61752106D59*')
 Assert ($keys.Count -eq 1) "actual installer $Label has one uninstall entry"
}
RunSetup 'fresh' $true
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
# Corrupt owned files to prove equal-version reinstall replaces actual bytes.
[IO.File]::WriteAllText((Join-Path $target 'VideoProcessor.exe'),'stale managed payload')
[IO.File]::WriteAllText((Join-Path $target 'vprenderer\VideoProcessorVPRenderer.dll'),'stale renderer')
RunSetup 'reinstall-remembered-path' $false
foreach($relative in $before.Keys){
 Assert ((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -eq $before[$relative]) "actual reinstall preserves $relative"
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
