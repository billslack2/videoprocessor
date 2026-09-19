$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'packaging\installer\install-support.ps1')
$testRoot = Join-Path $root ('artifacts\installer-support-tests-' + [guid]::NewGuid().ToString('N'))
$script:InstallRoot = Join-Path $testRoot 'custom install'
$null = New-Item -ItemType Directory -Path $InstallRoot -Force
$script:checks = 0
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++; Write-Host "PASS $Message"
}
function Throws([scriptblock]$Body, [string]$Pattern, [string]$Message) {
    $caught=$false
    try { & $Body | Out-Null } catch { if ($_.Exception.Message -notmatch $Pattern) { throw }; $caught=$true }
    Assert $caught $Message
}
function Put([string]$Relative, [string]$Value) {
    $file=Join-Path $InstallRoot $Relative
    $null=New-Item -ItemType Directory -Path (Split-Path -Parent $file) -Force
    [IO.File]::WriteAllText($file,$Value)
}
function Hash([string]$Relative) { (Get-FileHash -LiteralPath (Join-Path $InstallRoot $Relative)).Hash }
function Entry([string]$Path,[string]$Policy='managed') {
    [ordered]@{path=$Path;policy=$Policy;sha256=(Hash $Path)}
}
function SaveManifest($Entries,[string]$Path,[string]$Build) {
    [ordered]@{schemaVersion=1;applicationId='VideoProcessor-42D852F1-70E9-43ED-8739-D61752106D59';build=$Build;files=@($Entries)} |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding UTF8
}
Put 'VideoProcessor.exe' 'build-A'
Put 'vprenderer/VideoProcessorVPRenderer.dll' 'renderer-A'
Put 'config/retired.dll' 'retired-A'
Put 'VideoProcessor.cfg' ('# comments preserved' + [Environment]::NewLine + 'unknown.key = my value')
Put 'VideoProcessor.state' 'operator-state'
Put 'shaders/custom.hlsl' 'custom shader'
Put 'luts/custom.cube' 'custom LUT'
Put 'logs/operator.log' 'operator log'
$sentinels=@{}
foreach($name in @('VideoProcessor.cfg','VideoProcessor.state','shaders/custom.hlsl','luts/custom.cube','logs/operator.log')){$sentinels[$name]=Hash $name}
SaveManifest @((Entry 'VideoProcessor.exe'),(Entry 'vprenderer/VideoProcessorVPRenderer.dll'),(Entry 'config/retired.dll')) (Join-Path $InstallRoot 'INSTALL-MANIFEST.json') 'A'
$oldHost=Hash 'VideoProcessor.exe'
$oldRenderer=Hash 'vprenderer/VideoProcessorVPRenderer.dll'
Put 'VideoProcessor.exe' 'build-B'
Put 'vprenderer/VideoProcessorVPRenderer.dll' 'renderer-B'
$script:PayloadManifest=Join-Path $testRoot 'payload-B.json'
SaveManifest @((Entry 'VideoProcessor.exe'),(Entry 'vprenderer/VideoProcessorVPRenderer.dll'),(Entry 'VideoProcessor.cfg' 'seed')) $PayloadManifest 'B'
Put 'VideoProcessor.exe' 'build-A'
Put 'vprenderer/VideoProcessorVPRenderer.dll' 'renderer-A'
$script:Action='Prepare'
$script:BackupDirectory=Invoke-VpInstallAction
Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot 'config/retired.dll'))) 'obsolete owned DLL retired'
Put 'VideoProcessor.exe' 'build-B'
$script:Action='Verify'
Throws {Invoke-VpInstallAction} 'does not match' 'mixed host/renderer rejected'
Put 'vprenderer/VideoProcessorVPRenderer.dll' 'renderer-B'
$script:Action='Commit'
Invoke-VpInstallAction | Out-Null
Assert ((Get-Content (Join-Path $InstallRoot 'INSTALL-MANIFEST.json') -Raw | ConvertFrom-Json).build -eq 'B') 'selected build committed'
$script:Action='Restore'
Invoke-VpInstallAction | Out-Null
Assert ((Hash 'VideoProcessor.exe') -eq $oldHost -and (Hash 'vprenderer/VideoProcessorVPRenderer.dll') -eq $oldRenderer) 'recovery restores matching application pair'
Assert (Test-Path -LiteralPath (Join-Path $InstallRoot 'config/retired.dll')) 'recovery restores obsolete files'
$script:Action='Prepare'
$script:BackupDirectory=Invoke-VpInstallAction
Put 'VideoProcessor.exe' 'interrupted-write'
$script:Action='Check'
Invoke-VpInstallAction | Out-Null
Assert ((Hash 'VideoProcessor.exe') -eq $oldHost) 'retry recovers interrupted transaction'
foreach($name in $sentinels.Keys){Assert ((Hash $name) -eq $sentinels[$name]) "operator data unchanged: $name"}
Put 'config/private.dll' 'unknown'
Throws {Invoke-VpInstallAction} 'Unowned private DLL conflict: config\\private.dll' 'unknown DLL named and preserved'
Assert ((Get-Content (Join-Path $InstallRoot 'config/private.dll') -Raw) -eq 'unknown') 'unknown DLL not deleted'
Remove-Item -LiteralPath (Join-Path $InstallRoot 'config/private.dll')
Put 'config/retired.dll' 'modified'
Throws {Invoke-VpInstallAction} 'Previously managed file has been modified' 'modified obsolete file protected'
Put 'config/retired.dll' 'retired-A'
foreach($path in @('../escape.dll','C:\escape.dll','config/a:stream','config/../escape.dll')){
    Throws {Get-VpSafePath $InstallRoot $path} 'Unsafe|escapes' "unsafe path rejected: $path"
}
$bad=Get-Content $PayloadManifest -Raw | ConvertFrom-Json
$bad.files[2].policy='managed'
$bad | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $testRoot 'bad.json')
Throws {Read-VpInstallManifest (Join-Path $testRoot 'bad.json')} 'Operator data' 'manifest cannot claim active configuration'
Remove-Item -LiteralPath (Join-Path $InstallRoot 'INSTALL-MANIFEST.json')
@{schemaVersion=1;layoutVersion='VP-0107';files=@(@{destination='config/retired.dll'})} |
    ConvertTo-Json -Depth 5 | Set-Content (Join-Path $InstallRoot 'RELEASE-MANIFEST.json')
$script:Action='Prepare'
$script:BackupDirectory=Invoke-VpInstallAction
Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot 'config/retired.dll'))) 'ZIP manifest establishes obsolete binary ownership'
$script:Action='Restore'; Invoke-VpInstallAction | Out-Null
Write-Host "$checks preservation/recovery checks passed. Fixtures: $testRoot"
