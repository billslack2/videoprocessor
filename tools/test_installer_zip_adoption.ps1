[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$InstallerPath,
 [Parameter(Mandatory=$true)][string]$PortableRoot,
 [Parameter(Mandatory=$true)][string]$PayloadRoot
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
$installer=(Resolve-Path -LiteralPath $InstallerPath).Path
$PortableRoot=(Resolve-Path -LiteralPath $PortableRoot).Path
$payload=Get-Content -LiteralPath (Join-Path $PayloadRoot 'INSTALL-MANIFEST.json') -Raw | ConvertFrom-Json
$registry='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{42D852F1-70E9-43ED-8739-D61752106D59}_is1'
if(Test-Path $registry){throw 'Existing registration; refuse ZIP adoption test'}
foreach($preserveExtras in @($false,$true)) {
$testRoot=Join-Path $root ('artifacts\zip-adoption-'+[guid]::NewGuid().ToString('N'))
$target=Join-Path $testRoot 'Existing ZIP VP'
$null=New-Item -ItemType Directory -Path $target -Force
Get-ChildItem -LiteralPath $PortableRoot |
    Copy-Item -Destination $target -Recurse
Copy-Item -LiteralPath (Join-Path $target 'VideoProcessor.cfg.example') -Destination (Join-Path $target 'VideoProcessor.cfg')
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
$manifestPath=Join-Path $target 'RELEASE-MANIFEST.json'
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$manifest.files+= [pscustomobject]@{destination='config/obsolete-owned.dll'}
$manifest | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
$p=Start-Process -FilePath $installer -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/DIR="'+$target+'"'),('/LOG="'+(Join-Path $testRoot 'adopt.log')+'"')) -WindowStyle Hidden -Wait -PassThru
if($p.ExitCode -ne 0){throw "ZIP adoption failed: $($p.ExitCode)"}
if(Test-Path -LiteralPath (Join-Path $target 'config\obsolete-owned.dll')){throw 'Obsolete owned DLL not retired'}
foreach($relative in $before.Keys){
 if((Get-FileHash -LiteralPath (Join-Path $target $relative)).Hash -ne $before[$relative]){throw "Operator data changed: $relative"}
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
$p=Start-Process -FilePath (Join-Path $target 'unins000.exe') -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART') -WindowStyle Hidden -Wait -PassThru
if($p.ExitCode -ne 0 -or (Test-Path $registry)){throw 'ZIP adoption test cleanup failed'}
Write-Host "PASS real ZIP cleanup (preserve edited/user extras=$preserveExtras): selected custom location, exact managed hashes, obsolete owned DLL retired, config/state/profile/shader/LUT/log preserved, uninstall registration removed. Evidence: $testRoot"
}
