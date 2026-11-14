[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'installer_test_helpers.ps1')
$testRoot = Join-Path $root ('artifacts\isup-' + [guid]::NewGuid().ToString('N').Substring(0,8))
$helper = Join-Path $testRoot 'VideoProcessorInstallSupport.exe'
$null = New-Item -ItemType Directory -Path $testRoot -Force
& (Join-Path $PSScriptRoot 'build_installer_support.ps1') -FixtureIdentity -OutputPath $helper
if (-not (Test-Path -LiteralPath $helper)) { throw 'Compiled test helper was not produced.' }
$applicationId = 'VideoProcessor-0B37C49A-79D2-4D5D-9A7E-C99B31A2F001'
$configApplicationId = 'VideoProcessorConfig-7AD19C54-1F18-4227-8AF1-C99B31A2F002'
$fullGuid = '0B37C49A-79D2-4D5D-9A7E-C99B31A2F001'
$registryPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{' + $fullGuid + '}_is1'
if (Test-VpTestRegistration $fullGuid) { throw 'The isolated test identity is already registered; refusing to touch any registration.' }
$script:checks = 0
function Assert([bool]$Condition,[string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
    Write-Host "PASS $Message"
}
function Quote-Argument([string]$Value) {
    if ($Value.Contains('"')) { throw 'Fixture arguments must not contain quotation marks.' }
    return '"' + $Value + '"'
}
function Invoke-Helper([string]$Action,[string]$Manifest=$null,[string]$Backup=$null,[switch]$Portable,[string]$Id=$applicationId,[string]$Root=$script:InstallRoot) {
    $resultPath = Join-Path $testRoot ('result-' + [guid]::NewGuid().ToString('N').Substring(0,8) + '.txt')
    $stderrPath = $resultPath + '.stderr'
    $arguments = @('--action',$Action,'--application-id',$Id,'--install-root',$Root,'--result-path',$resultPath)
    if ($Manifest) { $arguments += @('--payload-manifest',$Manifest) }
    if ($Backup) { $arguments += @('--backup-directory',$Backup) }
    if ($Portable) { $arguments += '--portable-update' }
    $commandLine = ($arguments | ForEach-Object { Quote-Argument ([string]$_) }) -join ' '
    $process = Start-Process -FilePath $helper -ArgumentList $commandLine -WorkingDirectory $testRoot -WindowStyle Hidden -RedirectStandardError $stderrPath -Wait -PassThru
    $message = if (Test-Path -LiteralPath $resultPath) { [IO.File]::ReadAllText($resultPath) }
        elseif (Test-Path -LiteralPath $stderrPath) { [IO.File]::ReadAllText($stderrPath) } else { '' }
    if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath -Force }
    if (Test-Path -LiteralPath $stderrPath) { Remove-Item -LiteralPath $stderrPath -Force }
    return [pscustomobject]@{ ExitCode=$process.ExitCode; Message=$message }
}
function Expect-Failure($Result,[string]$Pattern,[string]$Message) {
    if ($Result.ExitCode -eq 0 -or $Result.Message -notmatch $Pattern) {
        throw ($Message + ' [exit=' + $Result.ExitCode + '; output=' + $Result.Message + ']')
    }
    $script:checks++
    Write-Host ('PASS ' + $Message)
}
function Put([string]$Relative,[string]$Value,[string]$Root=$script:InstallRoot) {
    $file = Join-Path $Root $Relative
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $file) -Force
    [IO.File]::WriteAllText($file,$Value,[Text.Encoding]::ASCII)
}
function Hash([string]$Relative,[string]$Root=$script:InstallRoot) {
    (Get-FileHash -LiteralPath (Join-Path $Root $Relative) -Algorithm SHA256).Hash.ToUpperInvariant()
}
function Entry([string]$Path,[string]$Policy='managed',[string]$Root=$script:InstallRoot) {
    [ordered]@{ path=$Path; policy=$Policy; sha256=(Hash $Path $Root) }
}
function Save-Manifest([string]$Path,[object[]]$Entries,[string]$Build,[string]$Id=$applicationId,[object[]]$Cleanup=@()) {
    $manifest = [ordered]@{ schemaVersion=1; applicationId=$Id; build=$Build; files=@($Entries) }
    if ($Cleanup.Count) { $manifest.cleanupFiles=@($Cleanup) }
    $manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $Path -Encoding UTF8
}
function Write-RawFixture([string]$Path,[string]$Text) {
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force
    [IO.File]::WriteAllText($Path,$Text,[Text.Encoding]::ASCII)
}

$script:InstallRoot = Join-Path $testRoot 'i'
$null = New-Item -ItemType Directory -Path $InstallRoot -Force
Put 'VideoProcessor.exe' 'build-A'
Put 'vprenderer/VideoProcessorVPRenderer.dll' 'renderer-A'
Put 'config/retired.dll' 'retired-A'
Put 'VideoProcessor.cfg' ("# comments preserved" + [Environment]::NewLine + "unknown.key = my value")
Put 'VideoProcessor.state' 'operator-state'
Put 'shaders/custom.hlsl' 'custom shader'
Put 'luts/custom.cube' 'custom LUT'
Put 'logs/operator.log' 'operator log'
$payloadRoot = Join-Path $testRoot 'payload'
$null = New-Item -ItemType Directory -Path (Join-Path $payloadRoot 'vprenderer') -Force
Put 'VideoProcessor.exe' 'build-B' $payloadRoot
Put 'vprenderer/VideoProcessorVPRenderer.dll' 'renderer-B' $payloadRoot
Put 'VideoProcessor.cfg' 'source seed must not replace user cfg' $payloadRoot
$oldManifestPath = Join-Path $testRoot 'installed-A.json'
Save-Manifest $oldManifestPath @(
    (Entry 'VideoProcessor.exe'),
    (Entry 'vprenderer/VideoProcessorVPRenderer.dll'),
    (Entry 'config/retired.dll'),
    (Entry 'VideoProcessor.cfg' 'seed')
) 'A'
Copy-Item -LiteralPath $oldManifestPath -Destination (Join-Path $InstallRoot 'INSTALL-MANIFEST.json')
$payloadManifest = Join-Path $testRoot 'payload-B.json'
$payloadFiles = @(
    (Entry 'VideoProcessor.exe' 'managed' $payloadRoot),
    (Entry 'vprenderer/VideoProcessorVPRenderer.dll' 'managed' $payloadRoot),
    (Entry 'VideoProcessor.cfg' 'seed' $payloadRoot)
)
$legacyHelperHash = 'AD78B0967E2F180FEC1833869B86D811C4D3E5608B1BF1351CE730045C043A64'
$cleanup = @(
    [ordered]@{ path='config/retired.dll'; sha256=(Hash 'config/retired.dll') },
    [ordered]@{ path='setup/install-support.ps1'; sha256=$legacyHelperHash }
)
Save-Manifest $payloadManifest $payloadFiles 'B' $applicationId $cleanup
$sentinels = @{}
foreach ($name in @('VideoProcessor.cfg','VideoProcessor.state','shaders/custom.hlsl','luts/custom.cube','logs/operator.log')) {
    $sentinels[$name] = Hash $name
}

# The helper operates on the selected package as a black box. A mixed host/renderer pair fails verification.
$result = Invoke-Helper 'Check' $payloadManifest
Assert ($result.ExitCode -eq 0) 'compiled helper checks a custom path'
$prepare = Invoke-Helper 'Prepare' $payloadManifest
Assert ($prepare.ExitCode -eq 0 -and (Test-Path -LiteralPath (Join-Path $prepare.Message 'transaction.json'))) 'compiled helper publishes a verified pending journal'
$backup = $prepare.Message
Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot 'config/retired.dll'))) 'obsolete owned DLL retired'
Put 'VideoProcessor.exe' 'build-B'
Expect-Failure (Invoke-Helper 'Verify' $payloadManifest) 'does not match' 'mixed host/renderer rejected'
Put 'vprenderer/VideoProcessorVPRenderer.dll' 'renderer-B'
Assert ((Invoke-Helper 'Verify' $payloadManifest).ExitCode -eq 0) 'compiled helper verifies the matching application pair'
Assert ((Invoke-Helper 'Commit' $payloadManifest $backup).ExitCode -eq 0) 'compiled helper commits the selected build'
Assert ((Get-Content (Join-Path $InstallRoot 'INSTALL-MANIFEST.json') -Raw | ConvertFrom-Json).build -eq 'B') 'selected build committed'
Assert ((Invoke-Helper 'Restore' $payloadManifest $backup).ExitCode -eq 0) 'compiled helper restores a committed transaction'
Assert (([IO.File]::ReadAllText((Join-Path $InstallRoot 'VideoProcessor.exe')) -eq 'build-A') -and
    ([IO.File]::ReadAllText((Join-Path $InstallRoot 'vprenderer/VideoProcessorVPRenderer.dll')) -eq 'renderer-A')) 'recovery restores matching application pair'
Assert (Test-Path -LiteralPath (Join-Path $InstallRoot 'config/retired.dll')) 'recovery restores obsolete files'
foreach ($name in $sentinels.Keys) { Assert ((Hash $name) -eq $sentinels[$name]) "operator data unchanged: $name" }

# Check automatically restores a pending transaction from a simulated interrupted write.
$prepare = Invoke-Helper 'Prepare' $payloadManifest
$backup = $prepare.Message
Put 'VideoProcessor.exe' 'interrupted-write'
Assert ((Invoke-Helper 'Check' $payloadManifest).ExitCode -eq 0) 'retry auto-recovers a pending transaction'
Assert ([IO.File]::ReadAllText((Join-Path $InstallRoot 'VideoProcessor.exe')) -eq 'build-A') 'pending recovery restores old host bytes'
Assert ((Get-Content (Join-Path $backup 'transaction.json') -Raw | ConvertFrom-Json).status -eq 'restored') 'pending recovery updates the RC6 journal status'

# Literal RC6 schemaVersion=1 pending journal fixture, with fixed, independently checked backup hashes.
$fixturePath = Join-Path $PSScriptRoot 'fixtures\rc6-installer-transaction.schema1.json'
$fixtureJson = [IO.File]::ReadAllText($fixturePath)
$installRootJson = (ConvertTo-Json -InputObject $InstallRoot -Compress).Trim('"')
$fixtureJson = $fixtureJson.Replace('__INSTALL_ROOT__',$installRootJson)
$rc6Backup = Join-Path $InstallRoot '.vp-installer-backups\rc6-literal-journal'
$null = New-Item -ItemType Directory -Path (Join-Path $rc6Backup 'files') -Force
Write-RawFixture (Join-Path $rc6Backup 'files\VideoProcessor.exe') 'rc6 previous host'
Write-RawFixture (Join-Path $rc6Backup 'files\vprenderer\VideoProcessorVPRenderer.dll') 'rc6 previous renderer'
[IO.File]::WriteAllText((Join-Path $rc6Backup 'transaction.json'),$fixtureJson,[Text.UTF8Encoding]::new($true))
Put 'VideoProcessor.exe' 'RC6 interrupted host'
Put 'vprenderer/VideoProcessorVPRenderer.dll' 'RC6 interrupted renderer'
Assert ((Invoke-Helper 'Check' $payloadManifest).ExitCode -eq 0) 'literal RC6 schema-1 pending journal is recovered'
Assert ([IO.File]::ReadAllText((Join-Path $InstallRoot 'VideoProcessor.exe')) -eq 'rc6 previous host') 'RC6 journal restores its host backup bytes'
Assert ([IO.File]::ReadAllText((Join-Path $InstallRoot 'vprenderer/VideoProcessorVPRenderer.dll')) -eq 'rc6 previous renderer') 'RC6 journal restores its renderer backup bytes'
Put 'VideoProcessor.exe' 'build-A'
Put 'vprenderer/VideoProcessorVPRenderer.dll' 'renderer-A'

# Restore validates all paths and hashes before it writes the first file.
$prepare = Invoke-Helper 'Prepare' $payloadManifest
$backup = $prepare.Message
Put 'VideoProcessor.exe' 'changed host before recovery'
Put 'vprenderer/VideoProcessorVPRenderer.dll' 'changed renderer before recovery'
Put 'files/vprenderer/VideoProcessorVPRenderer.dll' 'tampered saved renderer' $backup
Expect-Failure (Invoke-Helper 'Restore' $payloadManifest $backup) 'Backup failed verification' 'tampered journal backup rejected'
Assert ([IO.File]::ReadAllText((Join-Path $InstallRoot 'VideoProcessor.exe')) -eq 'changed host before recovery') 'tampered journal does not partially restore the first file'
Assert ([IO.File]::ReadAllText((Join-Path $InstallRoot 'vprenderer/VideoProcessorVPRenderer.dll')) -eq 'changed renderer before recovery') 'tampered journal leaves all destinations untouched'
Put 'files/vprenderer/VideoProcessorVPRenderer.dll' 'renderer-A' $backup
Assert ((Invoke-Helper 'Restore' $payloadManifest $backup).ExitCode -eq 0) 'valid transaction restores after tampered backup is repaired'

# Unsafe manifest paths and managed configuration claims are refused before mutation.
$badPathManifest = Join-Path $testRoot 'bad-path.json'
Save-Manifest $badPathManifest @([ordered]@{path='../escape.dll';policy='managed';sha256=('A' * 64)}) 'bad-path'
Expect-Failure (Invoke-Helper 'Check' $badPathManifest) 'Unsafe package path|escapes' 'unsafe manifest path rejected'
$badConfigManifest = Join-Path $testRoot 'bad-config.json'
Save-Manifest $badConfigManifest @([ordered]@{path='VideoProcessor.cfg';policy='managed';sha256=('A' * 64)}) 'bad-config'
Expect-Failure (Invoke-Helper 'Check' $badConfigManifest) 'Operator data' 'manifest cannot claim active configuration'

# A file locked with exclusive sharing blocks setup before any managed bytes change.
$lockPath = Join-Path $InstallRoot 'VideoProcessor.exe'
$lock = [IO.File]::Open($lockPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try { Expect-Failure (Invoke-Helper 'Check' $payloadManifest) 'used by another process|being used by another process|sharing violation' 'locked application file blocks setup preflight' }
finally { $lock.Dispose() }

# Unknown DLLs are quarantined byte-for-byte; modified old binaries are also recovered.
Put 'config/private.dll' 'unknown private bytes'
Assert ((Invoke-Helper 'Check' $payloadManifest).ExitCode -eq 0) 'unknown DLL does not block read-only preflight'
Assert (Test-Path -LiteralPath (Join-Path $InstallRoot 'config/private.dll')) 'preflight leaves unknown DLL untouched'
$prepare = Invoke-Helper 'Prepare' $payloadManifest
$backup = $prepare.Message
Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot 'config/private.dll'))) 'unknown DLL moved out of loader paths'
$recovered = Join-Path $InstallRoot ('recovered-files\' + (Split-Path -Leaf $backup) + '\config\private.dll')
Assert ([IO.File]::ReadAllText($recovered) -eq 'unknown private bytes') 'unknown DLL preserved byte-for-byte'
Assert ((Invoke-Helper 'Restore' $payloadManifest $backup).ExitCode -eq 0) 'unknown DLL transaction restores'
Put 'config/retired.dll' 'operator-edited obsolete DLL'
Assert ((Invoke-Helper 'Check' $payloadManifest).ExitCode -eq 0) 'modified obsolete file passes read-only preflight'
Assert ([IO.File]::ReadAllText((Join-Path $InstallRoot 'config/retired.dll')) -eq 'operator-edited obsolete DLL') 'preflight preserves a modified obsolete file'
$prepare = Invoke-Helper 'Prepare' $payloadManifest
$backup = $prepare.Message
$recovered = Join-Path $InstallRoot ('recovered-files\' + (Split-Path -Leaf $backup) + '\config\retired.dll')
Assert ([IO.File]::ReadAllText($recovered) -eq 'operator-edited obsolete DLL') 'modified obsolete binary copied to recovered-files'
Assert ((Invoke-Helper 'Restore' $payloadManifest $backup).ExitCode -eq 0) 'modified obsolete binary restores after recovery test'
Put 'config/retired.dll' 'retired-A'

# ZIP adoption uses the explicit VP-0107 inventory, not a DLL glob.
Remove-Item -LiteralPath (Join-Path $InstallRoot 'INSTALL-MANIFEST.json') -Force
@{schemaVersion=1;layoutVersion='VP-0107';files=@(@{destination='config/retired.dll'})} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $InstallRoot 'RELEASE-MANIFEST.json') -Encoding UTF8
$prepare = Invoke-Helper 'Prepare' $payloadManifest
$backup = $prepare.Message
Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot 'config/retired.dll'))) 'ZIP release inventory establishes obsolete binary ownership'
Assert ((Invoke-Helper 'Restore' $payloadManifest $backup).ExitCode -eq 0) 'ZIP adoption transaction restores'
Remove-Item -LiteralPath (Join-Path $InstallRoot 'RELEASE-MANIFEST.json') -Force
Copy-Item -LiteralPath $oldManifestPath -Destination (Join-Path $InstallRoot 'INSTALL-MANIFEST.json') -Force

# An exact old helper cleanup hash retires the prior bytes; edited text is retained.
$legacyFile = Join-Path $PSScriptRoot 'fixtures\legacy-install-support.rc6.ps1'
if ((Get-FileHash $legacyFile).Hash -ne $legacyHelperHash) { throw 'Tracked legacy helper bytes do not match the safe cleanup hash.' }
$null = New-Item -ItemType Directory -Path (Join-Path $InstallRoot 'setup') -Force
Copy-Item -LiteralPath $legacyFile -Destination (Join-Path $InstallRoot 'setup\install-support.ps1')
$oldWithHelper = Join-Path $testRoot "installed-A-with-helper.json"
Save-Manifest $oldWithHelper @(
    (Entry "VideoProcessor.exe"),
    (Entry "vprenderer/VideoProcessorVPRenderer.dll"),
    (Entry "config/retired.dll"),
    (Entry "VideoProcessor.cfg" "seed")
) "A-with-legacy-setup-helper" $applicationId @([ordered]@{path="setup/install-support.ps1";sha256=$legacyHelperHash})
Copy-Item -LiteralPath $oldWithHelper -Destination (Join-Path $InstallRoot "INSTALL-MANIFEST.json") -Force
$prepare = Invoke-Helper 'Prepare' $payloadManifest
$backup = $prepare.Message
Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot 'setup\install-support.ps1'))) 'exact old setup-helper bytes retired'
Assert ((Invoke-Helper 'Restore' $payloadManifest $backup).ExitCode -eq 0) 'legacy helper cleanup transaction restores'
Add-Content -LiteralPath (Join-Path $InstallRoot 'setup\install-support.ps1') -Value '# user edit'
$prepare = Invoke-Helper 'Prepare' $payloadManifest
$backup = $prepare.Message
Assert (Test-Path -LiteralPath (Join-Path $InstallRoot 'setup\install-support.ps1')) 'edited legacy setup helper retained'
Assert ((Invoke-Helper 'Restore' $payloadManifest $backup).ExitCode -eq 0) 'edited legacy helper remains after restore'

# Identity checks are bound to compile-time fixture constants, never a runtime bypass.
Expect-Failure (Invoke-Helper 'Check' $payloadManifest -Id 'VideoProcessor-arbitrary-test-identity') 'identity is not supported' 'arbitrary runtime application identity rejected'
$configRoot = Join-Path $testRoot 'standalone config'
$null = New-Item -ItemType Directory -Path $configRoot -Force
$configManifest = Join-Path $testRoot 'config-product.json'
$null = New-Item -ItemType Directory -Path (Join-Path $payloadRoot 'config') -Force
Put 'config/VideoProcessorConfig.exe' 'config B' $payloadRoot
Save-Manifest $configManifest @((Entry 'config/VideoProcessorConfig.exe' 'managed' $payloadRoot)) 'config-only' $configApplicationId
Assert ((Invoke-Helper 'Check' $configManifest -Id $configApplicationId -Root $configRoot).ExitCode -eq 0) 'isolated Config-only product identity accepts its folder'
Expect-Failure (Invoke-Helper 'Check' $payloadManifest -Id $configApplicationId) 'payload identity' 'Config helper identity rejects full VP payload'

# Isolated HKCU fixture registration checks do not read, remove or replace the real product entry.
try {
    $null = New-Item -Path $registryPath -Force
    $null = New-ItemProperty -Path $registryPath -Name 'Inno Setup: App Path' -Value (Join-Path $testRoot 'registered elsewhere') -PropertyType String -Force
    Expect-Failure (Invoke-Helper 'Check' $payloadManifest) 'registered at' 'registered full install cannot be moved'
    $null = Set-ItemProperty -Path $registryPath -Name 'Inno Setup: App Path' -Value $InstallRoot
    Expect-Failure (Invoke-Helper 'Check' $payloadManifest -Portable) 'Portable update cannot modify a registered' 'portable mode cannot modify registered root'
    $null = Set-ItemProperty -Path $registryPath -Name 'Inno Setup: App Path' -Value (Join-Path $testRoot 'registered elsewhere')
    Assert ((Invoke-Helper 'Check' $payloadManifest -Portable).ExitCode -eq 0) 'portable update permits another fixture registration elsewhere'
}
finally {
    if (Test-VpTestRegistration $fullGuid) { Remove-Item -LiteralPath $registryPath -Recurse -Force }
}

# Unsafe journal paths are rejected without altering an outside marker.
$unsafeBackup = Join-Path $InstallRoot '.vp-installer-backups\unsafe-journal'
$null = New-Item -ItemType Directory -Path (Join-Path $unsafeBackup 'files') -Force
$unsafeJournal = '{"schemaVersion":1,"installRoot":"' + $installRootJson + '","status":"pending","build":"unsafe","files":[{"path":"../escape.txt","existed":false,"sha256":""}]}'
[IO.File]::WriteAllText((Join-Path $unsafeBackup 'transaction.json'),$unsafeJournal,[Text.UTF8Encoding]::new($true))
$outside = Join-Path $testRoot 'escape.txt'
Put 'escape.txt' 'outside marker' $testRoot
Expect-Failure (Invoke-Helper 'Restore' $payloadManifest $unsafeBackup) 'Unsafe package path|escapes' 'unsafe journal path rejected'
Assert ([IO.File]::ReadAllText($outside) -eq 'outside marker') 'unsafe journal cannot alter an outside path'
Remove-Item -LiteralPath $unsafeBackup -Recurse -Force

# Mixed historical statuses and unknown/modified backup contents are never pruned.
$finalPrepare = Invoke-Helper 'Prepare' $payloadManifest
$finalBackup = $finalPrepare.Message
Put 'VideoProcessor.exe' 'build-B'
Put 'vprenderer/VideoProcessorVPRenderer.dll' 'renderer-B'
Assert ((Invoke-Helper 'Commit' $payloadManifest $finalBackup).ExitCode -eq 0) 'final fixture transaction commits'
$unknownBackupFile = Join-Path $finalBackup 'my-backup.bak'
[IO.File]::WriteAllText($unknownBackupFile,'keep this user backup')
Assert ((Invoke-Helper 'Finalize' $payloadManifest $finalBackup).ExitCode -eq 0) 'finalize succeeds with unknown backup data'
Assert (Test-Path -LiteralPath $unknownBackupFile) 'unrecognized recovery file is not deleted'
Assert (Test-Path -LiteralPath (Join-Path $finalBackup 'transaction.json')) 'unknown backup retains its recovery journal'
Remove-Item -LiteralPath $unknownBackupFile -Force
$savedHost = Join-Path $finalBackup 'files\VideoProcessor.exe'
[IO.File]::WriteAllText($savedHost,'tampered backup')
Assert ((Invoke-Helper 'Finalize' $payloadManifest $finalBackup).ExitCode -eq 0) 'finalize leaves a modified backup available'
Assert (Test-Path -LiteralPath $savedHost) 'modified backup bytes are retained'
[IO.File]::WriteAllText($savedHost,'build-A')
Assert ((Invoke-Helper 'Finalize' $payloadManifest $finalBackup).ExitCode -eq 0) 'cleanup resumes after exact backup bytes are restored'
Assert (-not (Test-Path -LiteralPath (Join-Path $InstallRoot '.vp-installer-backups'))) 'verified completed and restored history pruned'
foreach ($name in $sentinels.Keys) { Assert ((Hash $name) -eq $sentinels[$name]) "cleanup preserves operator data: $name" }
Write-Host "$checks compiled-helper checks passed. Helper and evidence: $testRoot"
