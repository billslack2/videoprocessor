$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $root 'packaging\installer\install-support.ps1')
# Filesystem fixtures do not launch application binaries. Actual process refusal
# is exercised by test_installer_e2e.ps1 against the real Config executable.
function Assert-VpClosed {}
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
Invoke-VpInstallAction | Out-Null
Assert $true 'unknown DLL does not block repair preflight'
Assert ((Get-Content (Join-Path $InstallRoot 'config/private.dll') -Raw) -eq 'unknown') 'unknown DLL not deleted'
Remove-Item -LiteralPath (Join-Path $InstallRoot 'config/private.dll')
Put 'config/retired.dll' 'modified'
Invoke-VpInstallAction | Out-Null
Assert ((Get-Content (Join-Path $InstallRoot 'config/retired.dll') -Raw) -eq 'modified') 'preflight leaves modified obsolete file intact'
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
# Upgrade from the old installer: seed examples are removable only at the
# recorded original hash, and modified setup documents become user-owned.
$extras=@('prerequisites/setup-runtime.ps1','setup/install-support.ps1','setup/RECOVERY.md',
          'docs/VP-0174-config-ui.md','VideoProcessor.cfg.example')
foreach($name in $extras){Put $name 'original package extra'}
$legacyEntries=@((Entry 'VideoProcessor.exe'),(Entry 'vprenderer/VideoProcessorVPRenderer.dll'),(Entry 'config/retired.dll'))
$legacyEntries+=@(foreach($name in $extras){Entry $name $(if($name -eq 'VideoProcessor.cfg.example'){'seed'}else{'managed'})})
SaveManifest $legacyEntries (Join-Path $InstallRoot 'INSTALL-MANIFEST.json') 'old-full-installer'
$next=Get-Content $PayloadManifest -Raw | ConvertFrom-Json
$cleanup=@(foreach($name in $extras){@{path=$name;sha256=(Hash $name)}})
$next | Add-Member -NotePropertyName cleanupFiles -NotePropertyValue $cleanup
$next | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $PayloadManifest -Encoding UTF8
Put 'docs/VP-0174-config-ui.md' 'operator notes in an edited developer doc'
Put 'prerequisites/private.bak' 'operator backup, never an installer artifact'
$script:Action='Prepare'; $script:BackupDirectory=Invoke-VpInstallAction
foreach($name in $extras | Where-Object {$_ -ne 'docs/VP-0174-config-ui.md'}){
    Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot $name))) "old package extra retired: $name"
}
Assert ((Get-Content (Join-Path $InstallRoot 'docs/VP-0174-config-ui.md') -Raw) -like 'operator notes*') 'edited developer documentation preserved'
Assert (Test-Path -LiteralPath (Join-Path $InstallRoot 'prerequisites/private.bak')) 'user backup in prerequisite folder preserved'
$script:Action='Finalize'
Throws {Invoke-VpInstallAction} 'not committed' 'pending transaction cannot be pruned'
Put 'VideoProcessor.exe' 'build-B'
Put 'vprenderer/VideoProcessorVPRenderer.dll' 'renderer-B'
$script:Action='Commit'; Invoke-VpInstallAction | Out-Null
Assert (Test-Path -LiteralPath $BackupDirectory) 'rollback remains available until setup finishes'
$script:Action='Finalize'; Invoke-VpInstallAction | Out-Null
Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot '.vp-installer-backups'))) 'successful setup prunes current and historical verified backups'
Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot 'setup'))) 'empty setup directory removed'
Assert (Test-Path -LiteralPath (Join-Path $InstallRoot 'prerequisites/private.bak')) 'nonempty prerequisite directory retained for user backup'
Remove-Item -LiteralPath (Join-Path $InstallRoot 'prerequisites/private.bak')
$script:Action='Prepare'; $script:BackupDirectory=Invoke-VpInstallAction
$script:Action='Commit'; Invoke-VpInstallAction | Out-Null
$unknown=Join-Path $BackupDirectory 'my-backup.bak'
[IO.File]::WriteAllText($unknown,'keep this user file')
$script:Action='Finalize'; Invoke-VpInstallAction | Out-Null
Assert (Test-Path -LiteralPath $unknown) 'unrecognized files inside recovery folder preserved'
Assert (Test-Path -LiteralPath (Join-Path $BackupDirectory 'transaction.json')) 'unknown backup retains its recovery journal'
Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot 'prerequisites'))) 'empty prerequisite directory removed'
Remove-Item -LiteralPath $unknown
$savedHost=Join-Path $BackupDirectory 'files/VideoProcessor.exe'
[IO.File]::WriteAllText($savedHost,'modified backup')
Invoke-VpInstallAction | Out-Null
Assert (Test-Path -LiteralPath $savedHost) 'modified recovery backup not deleted'
[IO.File]::WriteAllText($savedHost,'build-B')
Invoke-VpInstallAction | Out-Null
Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot '.vp-installer-backups'))) 'verified backup cleanup resumes when unknown data removed'
foreach($name in $sentinels.Keys){Assert ((Hash $name) -eq $sentinels[$name]) "cleanup preserves operator data: $name"}
# Repair a destroyed manifest and an unowned DLL without losing either's bytes.
Put 'INSTALL-MANIFEST.json' 'corrupted ownership JSON'
Put 'config/unknown-old.dll' 'old private dependency'
$script:Action='Prepare';$script:BackupDirectory=Invoke-VpInstallAction
Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot 'config/unknown-old.dll'))) 'conflicting unknown DLL moved out of loader paths during repair'
$recovered=@(Get-ChildItem -LiteralPath (Join-Path $InstallRoot 'recovered-files') -Recurse -Filter 'unknown-old.dll' -File)
Assert ($recovered.Count -eq 1 -and [IO.File]::ReadAllText($recovered[0].FullName) -eq 'old private dependency') 'unknown DLL preserved byte-for-byte in local recovery folder'
$script:Action='Commit';Invoke-VpInstallAction | Out-Null
$script:Action='Finalize';Invoke-VpInstallAction | Out-Null
Assert ((Read-VpInstallManifest (Join-Path $InstallRoot 'INSTALL-MANIFEST.json')).build -eq 'B') 'corrupt manifest repaired'
foreach($name in $sentinels.Keys){Assert ((Hash $name) -eq $sentinels[$name]) "manifest repair preserves $name"}
Write-Host "$checks preservation/recovery checks passed. Fixtures: $testRoot"
