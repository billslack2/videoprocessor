[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent $PSScriptRoot
$scratch = Join-Path $env:TEMP ('vp-update-handoff-' + [guid]::NewGuid().ToString('N'))
$installRoot = Join-Path $scratch 'install'
$handoffRoot = Join-Path $scratch 'handoffs'
$fixtureExe = Join-Path $scratch 'UpdateHandoffFixture.exe'
$publicKeyPath = Join-Path $scratch 'test-public-key.xml'
$privateKeyPath = Join-Path $scratch 'test-private-key.xml'
$specPath = Join-Path $scratch 'fixture.json'
$parentReadyPath = Join-Path $scratch 'parent-ready.txt'
$parentErrorPath = Join-Path $scratch 'parent-error.txt'
$setupCountPath = Join-Path $scratch 'setup-count.txt'
$setupObservationPath = Join-Path $scratch 'setup-observation.json'
$playerStartCountPath = Join-Path $scratch 'player-start-count.txt'
$stderrPath = Join-Path $scratch 'parent.stderr'
$previousSpec = $env:VP_HANDOFF_FIXTURE_SPEC
$script:checks = 0
$parent = $null
$duplicate = $null
$player = $null
$playerPath = $null
$cleaner = $null
$handoffDirectory = $null
$diagnosticCache = $null
$diagnosticFilesOwned = $false
$rsa = $null
$testCache = $null
$testPreferencePath = $null
$testPreferencesOwned = $false
$stagedDirectories = New-Object System.Collections.ArrayList
function Assert([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
    Write-Host "PASS $Message"
}
function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function Hash-Bytes([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function New-TestEnvelope([string]$Name) {
    $releasePayload.notes = $Name
    $payloadText = $releasePayload | ConvertTo-Json -Depth 8 -Compress
    $payloadBytes = [Text.UTF8Encoding]::new($false).GetBytes($payloadText)
    $signature = [Convert]::ToBase64String($rsa.SignData($payloadBytes, [Security.Cryptography.CryptoConfig]::MapNameToOID('SHA256')))
    $envelopeText = ([ordered]@{ payload = [Convert]::ToBase64String($payloadBytes); signature = $signature } | ConvertTo-Json -Compress)
    $path = Join-Path $scratch ($Name + '.signed-release.json')
    Write-Utf8NoBom $path $envelopeText
    $envelopeHash = Hash-Bytes ([Text.UTF8Encoding]::new($false).GetBytes($envelopeText))
    $identity = $installRoot.ToLowerInvariant() + '|' + $sourceUpdaterHash + '|' + $envelopeHash + '|' + $installerHash + '|' + $newSequence
    $id = Hash-Bytes ([Text.UTF8Encoding]::new($false).GetBytes($identity))
    $caseRoot = Join-Path $scratch ('handoffs-' + $Name)
    return [pscustomobject]@{ Path = $path; Id = $id; Root = $caseRoot; Directory = (Join-Path $caseRoot $id) }
}
function Wait-Path([string]$Path, [Diagnostics.Process]$Process, [int]$TimeoutMs, [string]$FailureMessage) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.ElapsedMilliseconds -lt $TimeoutMs) {
        if (Test-Path -LiteralPath $Path) { return }
        if ($Process -and $Process.HasExited) {
            $detail = if (Test-Path -LiteralPath $parentErrorPath) { [IO.File]::ReadAllText($parentErrorPath) }
                elseif (Test-Path -LiteralPath $stderrPath) { [IO.File]::ReadAllText($stderrPath) } else { '' }
            throw ($FailureMessage + ' Parent exited ' + $Process.ExitCode + '. ' + $detail)
        }
        Start-Sleep -Milliseconds 50
    }
    throw $FailureMessage
}
function Reset-TestSignals {
    foreach ($path in @($parentReadyPath, $parentErrorPath, $setupCountPath, $setupObservationPath, $stderrPath)) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
}
function Wait-NoStageProcess([string]$Path, [int]$TimeoutMs) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    do {
        $processes = Get-ExactImageProcesses $Path
        $count = $processes.Count
        foreach ($process in $processes) { $process.Dispose() }
        if ($count -eq 0) { return $true }
        Start-Sleep -Milliseconds 50
    } while ($clock.ElapsedMilliseconds -lt $TimeoutMs)
    return $false
}
function Get-ExactImageProcesses([string]$Path, [string]$ProcessName = 'VideoProcessorUpdate') {
    $matches = @()
    foreach ($process in [Diagnostics.Process]::GetProcessesByName($ProcessName)) {
        try {
            if ([IO.Path]::GetFullPath($process.MainModule.FileName).Equals([IO.Path]::GetFullPath($Path), [StringComparison]::OrdinalIgnoreCase)) {
                $matches += $process
            } else { $process.Dispose() }
        } catch { $process.Dispose() }
    }
    return ,$matches
}
try {
    $null = New-Item -ItemType Directory -Path $installRoot -Force
    $null = New-Item -ItemType Directory -Path $handoffRoot -Force

    $rsa = New-Object Security.Cryptography.RSACryptoServiceProvider(2048)
    Write-Utf8NoBom $publicKeyPath $rsa.ToXmlString($false)
    Write-Utf8NoBom $privateKeyPath $rsa.ToXmlString($true)

    $compiler = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    if (-not (Test-Path -LiteralPath $compiler)) { throw 'The x64 .NET Framework C# compiler is required.' }
    $sources = @(
        (Join-Path $repoRoot 'src\VideoProcessor-Update\UpdateCore.cs'),
        (Join-Path $repoRoot 'src\VideoProcessor-Update\Program.cs'),
        (Join-Path $repoRoot 'src\VideoProcessor-Update\UpdateControl.cs'),
        (Join-Path $repoRoot 'src\VideoProcessor-Update\InstallHandoff.cs'),
        (Join-Path $PSScriptRoot 'UpdateHandoffFixture.cs')
    )
    $compilerArgs = @('/nologo','/target:winexe','/platform:x64','/optimize+','/define:VP_TEST_HANDOFF','/main:UpdateHandoffFixture',"/out:$fixtureExe",
        '/r:System.Web.Extensions.dll','/r:System.Windows.Forms.dll','/r:System.Drawing.dll','/r:System.Management.dll',"/resource:$publicKeyPath,UpdatePublicKey.xml")
    $compilerArgs += $sources
    & $compiler @compilerArgs
    if ($LASTEXITCODE -ne 0) { throw 'Test-key updater fixture compilation failed.' }

    $updaterPath = Join-Path $installRoot 'VideoProcessorUpdate.exe'
    Copy-Item -LiteralPath $fixtureExe -Destination $updaterPath
    $sourceUpdaterHash = (Get-FileHash -LiteralPath $updaterPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $sourceCommit = 'a' * 40
    $targetCommit = 'b' * 40
    $oldSequence = 1
    $newSequence = 2
    $applicationId = 'VideoProcessor-42D852F1-70E9-43ED-8739-D61752106D59'
    $configContents = '# keep the user exact comments' + [Environment]::NewLine + 'custom.value = preserve this' + [Environment]::NewLine
    $stateContents = 'operator state sentinel' + [Environment]::NewLine
    Write-Utf8NoBom (Join-Path $installRoot 'VideoProcessor.cfg') $configContents
    Write-Utf8NoBom (Join-Path $installRoot 'VideoProcessor.state') $stateContents
    $configHashBefore = (Get-FileHash -LiteralPath (Join-Path $installRoot 'VideoProcessor.cfg') -Algorithm SHA256).Hash
    $stateHashBefore = (Get-FileHash -LiteralPath (Join-Path $installRoot 'VideoProcessor.state') -Algorithm SHA256).Hash

    $installedManifest = [ordered]@{
        schemaVersion = 1; applicationId = $applicationId; flavor = 'full'
        coreVersion = 'handoff-old'; sourceCommit = $sourceCommit
        sourceFingerprint = ('c' * 64); dirty = $false; updateSequence = $oldSequence
        updateChannel = 'beta'; rpcVersion = 2; configurationVersion = 1
        files = @([ordered]@{ path = 'VideoProcessorUpdate.exe'; policy = 'managed'; sha256 = $sourceUpdaterHash })
    }
    Write-Utf8NoBom (Join-Path $installRoot 'INSTALL-MANIFEST.json') ($installedManifest | ConvertTo-Json -Depth 8 -Compress)

    $replacementUpdaterBytes = [Text.Encoding]::ASCII.GetBytes('new managed updater bytes for lock proof')
    $replacementUpdaterHash = Hash-Bytes $replacementUpdaterBytes
    $newManifestObject = [ordered]@{
        schemaVersion = 1; applicationId = $applicationId; flavor = 'full'
        coreVersion = 'handoff-new'; sourceCommit = $targetCommit
        sourceFingerprint = ('d' * 64); dirty = $false; updateSequence = $newSequence
        updateChannel = 'beta'; rpcVersion = 2; configurationVersion = 1
        files = @([ordered]@{ path = 'VideoProcessorUpdate.exe'; policy = 'managed'; sha256 = $replacementUpdaterHash })
    }
    $newManifestText = $newManifestObject | ConvertTo-Json -Depth 8 -Compress
    $newManifestBytes = [Text.UTF8Encoding]::new($false).GetBytes($newManifestText)
    $newManifestHash = Hash-Bytes $newManifestBytes

    $installerHash = (Get-FileHash -LiteralPath $fixtureExe -Algorithm SHA256).Hash.ToLowerInvariant()
    $installerSize = (Get-Item -LiteralPath $fixtureExe).Length
    $releasePayload = [ordered]@{
        schemaVersion = 1; minimumUpdaterVersion = 1; sequence = $newSequence
        version = 'handoff-fixture'; commit = $targetCommit; channel = 'beta'
        architecture = 'x64'; expiresUtc = [DateTime]::UtcNow.AddDays(7).ToString('o')
        rpcVersion = 2; configurationVersion = 1; notes = 'isolated handoff fixture'
        packages = @([ordered]@{
            flavor = 'full'
            url = 'https://github.com/billslack2/videoprocessor/releases/download/handoff-fixture/VideoProcessorSetup.exe'
            sha256 = $installerHash; size = $installerSize; installManifestSha256 = $newManifestHash
        })
    }
    $payloadText = $releasePayload | ConvertTo-Json -Depth 8 -Compress
    $payloadBytes = [Text.UTF8Encoding]::new($false).GetBytes($payloadText)
    $rsa.FromXmlString([IO.File]::ReadAllText($privateKeyPath))
    $signature = [Convert]::ToBase64String($rsa.SignData($payloadBytes, [Security.Cryptography.CryptoConfig]::MapNameToOID('SHA256')))
    $envelopeText = ([ordered]@{
        payload = [Convert]::ToBase64String($payloadBytes); signature = $signature
    } | ConvertTo-Json -Compress)
    $envelopePath = Join-Path $scratch 'signed-release.json'
    Write-Utf8NoBom $envelopePath $envelopeText
    $envelopeHash = Hash-Bytes ([Text.UTF8Encoding]::new($false).GetBytes($envelopeText))

    $identity = $installRoot.ToLowerInvariant() + '|' + $sourceUpdaterHash + '|' + $envelopeHash + '|' + $installerHash + '|' + $newSequence
    $handoffId = Hash-Bytes ([Text.UTF8Encoding]::new($false).GetBytes($identity))
    $handoffDirectory = Join-Path $handoffRoot $handoffId
$null = $stagedDirectories.Add($handoffDirectory)
    $spec = [ordered]@{
        installRoot = $installRoot; handoffRoot = $handoffRoot
        envelopePath = $envelopePath; installerPath = $fixtureExe
        parentReadyPath = $parentReadyPath; parentErrorPath = $parentErrorPath
        setupCountPath = $setupCountPath; setupObservationPath = $setupObservationPath; playerStartCountPath = $playerStartCountPath
        cleanupCompletePath = (Join-Path $scratch 'cleanup-complete.txt')
        replacementUpdaterBase64 = [Convert]::ToBase64String($replacementUpdaterBytes)
        newManifestBase64 = [Convert]::ToBase64String($newManifestBytes)
        delayAfterReadyMs = 0; checkpointPhase = ''; checkpointAction = ''; checkpointDelayMs = 0
        unattended = $false; setupExitCode = 0
    }
    Write-Utf8NoBom $specPath ($spec | ConvertTo-Json -Depth 8 -Compress)
    $env:VP_HANDOFF_FIXTURE_SPEC = $specPath

    $oldManifestHash = (Get-FileHash -LiteralPath (Join-Path $installRoot 'INSTALL-MANIFEST.json') -Algorithm SHA256).Hash
    $spec.checkpointPhase = 'staged-before-parent-locks'
    $spec.checkpointAction = 'tamper-setup'
    Write-Utf8NoBom $specPath ($spec | ConvertTo-Json -Depth 8 -Compress)
    $argumentLine = '--fixture-parent "' + $specPath + '"'
    $parent = Start-Process -FilePath $updaterPath -ArgumentList $argumentLine -WorkingDirectory $installRoot -WindowStyle Hidden -RedirectStandardError $stderrPath -PassThru
    Wait-Path $parentErrorPath $parent 20000 'Tampered staged setup was not rejected by the coordinator.'
    Assert ($parent.WaitForExit(10000)) 'coordinator rejects changed staged setup before READY'
    Assert (([IO.File]::ReadAllText($parentErrorPath)).Contains('Staged handoff contents failed verification')) 'parent detects staged installer mutation after its initial inventory check'
    Assert (-not (Test-Path -LiteralPath $parentReadyPath) -and -not (Test-Path -LiteralPath $setupCountPath)) 'tampered handoff never reaches READY or invokes setup'
    Assert ((Get-FileHash -LiteralPath $updaterPath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceUpdaterHash) 'tampered handoff leaves installed updater unchanged'
    Assert ((Get-FileHash -LiteralPath (Join-Path $installRoot 'INSTALL-MANIFEST.json') -Algorithm SHA256).Hash -eq $oldManifestHash) 'tampered handoff leaves installed manifest unchanged'
    $stagedProcesses = Get-ExactImageProcesses (Join-Path $handoffDirectory 'VideoProcessorUpdate.exe')
    Assert ($stagedProcesses.Count -eq 0) 'tampered handoff starts no staged updater child'
    foreach ($process in $stagedProcesses) { $process.Dispose() }
    $parent.Dispose(); $parent = $null
    Remove-Item -LiteralPath $parentErrorPath -Force

    $childErrorCase = New-TestEnvelope 'child-rejected-before-ready'
    $null = $stagedDirectories.Add($childErrorCase.Directory)
    Reset-TestSignals
    $spec.envelopePath = $childErrorCase.Path; $spec.handoffRoot = $childErrorCase.Root
    $spec.checkpointPhase = 'child-validated-before-ready'; $spec.checkpointAction = 'throw'; $spec.checkpointDelayMs = 0
    Write-Utf8NoBom $specPath ($spec | ConvertTo-Json -Depth 8 -Compress)
    $parent = Start-Process -FilePath $updaterPath -ArgumentList $argumentLine -WorkingDirectory $installRoot -WindowStyle Hidden -RedirectStandardError $stderrPath -PassThru
    Wait-Path $parentErrorPath $parent 20000 'Child pre-READY rejection did not reach the coordinator.'
    Assert ($parent.WaitForExit(10000)) 'coordinator exits after authenticated child rejection'
    Assert ([IO.File]::ReadAllText($parentErrorPath).Contains('Fixture rejected readiness')) 'child failure is returned over the authenticated handoff pipe'
    Assert (-not (Test-Path -LiteralPath $parentReadyPath) -and -not (Test-Path -LiteralPath $setupCountPath)) 'child pre-READY failure never signals READY or runs setup'
    Assert (Wait-NoStageProcess (Join-Path $childErrorCase.Directory 'VideoProcessorUpdate.exe') 5000) 'rejected staged child exits without lingering'
    Assert ((Get-FileHash -LiteralPath $updaterPath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceUpdaterHash) 'child rejection preserves installed updater bytes'
    $parent.Dispose(); $parent = $null; Remove-Item -LiteralPath $parentErrorPath -Force

    $timeoutCase = New-TestEnvelope 'child-readiness-timeout'
    $null = $stagedDirectories.Add($timeoutCase.Directory)
    Reset-TestSignals
    $spec.envelopePath = $timeoutCase.Path; $spec.handoffRoot = $timeoutCase.Root
    $spec.checkpointPhase = 'child-validated-before-ready'; $spec.checkpointAction = 'sleep'; $spec.checkpointDelayMs = 8000
    Write-Utf8NoBom $specPath ($spec | ConvertTo-Json -Depth 8 -Compress)
    $parent = Start-Process -FilePath $updaterPath -ArgumentList $argumentLine -WorkingDirectory $installRoot -WindowStyle Hidden -RedirectStandardError $stderrPath -PassThru
    Wait-Path $parentErrorPath $parent 20000 'Readiness timeout did not abort parent startup.'
    Assert ($parent.WaitForExit(10000)) 'coordinator exits after readiness timeout'
    Assert ([IO.File]::ReadAllText($parentErrorPath).Contains('did not become ready')) 'child that stalls before READY is rejected on the handshake deadline'
    Assert (-not (Test-Path -LiteralPath $parentReadyPath) -and -not (Test-Path -LiteralPath $setupCountPath)) 'readiness timeout never reaches READY or invokes setup'
    Assert (Wait-NoStageProcess (Join-Path $timeoutCase.Directory 'VideoProcessorUpdate.exe') 5000) 'timed-out staged child is terminated'
    Assert ((Get-FileHash -LiteralPath $updaterPath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceUpdaterHash) 'readiness timeout preserves installed updater bytes'
    $parent.Dispose(); $parent = $null; Remove-Item -LiteralPath $parentErrorPath -Force

    $fixtureRootId = Hash-Bytes ([Text.UTF8Encoding]::new($false).GetBytes([Security.Principal.WindowsIdentity]::GetCurrent().User.Value + '|' + $installRoot.ToLowerInvariant()))
    $testCache = Join-Path (Join-Path $env:LOCALAPPDATA 'VideoProcessor\Updates') $fixtureRootId
    $testPreferencePath = Join-Path $testCache 'preferences.json'
    if (Test-Path -LiteralPath $testPreferencePath) { throw 'Unique fixture root unexpectedly has an existing updater preference file.' }
    $null = New-Item -ItemType Directory -Path $testCache -Force
    Write-Utf8NoBom $testPreferencePath '{"automatic":true,"mode":"auto","channel":"beta","allowPlaybackRestart":true,"skippedSequence":0}'
    $testPreferencesOwned = $true

    $parentWaitCase = New-TestEnvelope 'parent-exit-timeout'
    $null = $stagedDirectories.Add($parentWaitCase.Directory)
    Reset-TestSignals
    $spec.envelopePath = $parentWaitCase.Path; $spec.handoffRoot = $parentWaitCase.Root
    $spec.checkpointPhase = ''; $spec.checkpointAction = ''; $spec.checkpointDelayMs = 0
    $spec.unattended = $true; $spec.delayAfterReadyMs = 5000; $spec.setupExitCode = 0
    Write-Utf8NoBom $specPath ($spec | ConvertTo-Json -Depth 8 -Compress)
    $parent = Start-Process -FilePath $updaterPath -ArgumentList $argumentLine -WorkingDirectory $installRoot -WindowStyle Hidden -RedirectStandardError $stderrPath -PassThru
    Wait-Path $parentReadyPath $parent 20000 'Parent-exit timeout case did not receive READY.'
    Assert (-not $parent.HasExited) 'coordinator remains alive after READY for the parent-exit timeout case'
    $duplicate = Start-Process -FilePath $updaterPath -ArgumentList $argumentLine -WorkingDirectory $installRoot -WindowStyle Hidden -RedirectStandardError (Join-Path $scratch 'duplicate.stderr') -PassThru
    Wait-Path $parentErrorPath $duplicate 10000 'Duplicate coordinator was not rejected while the first coordinator remained active.'
    Assert ($duplicate.WaitForExit(10000)) 'duplicate coordinator exits while the original owns the root singleton'
    Assert ([IO.File]::ReadAllText($parentErrorPath).Contains('could not acquire the updater coordinator mutex')) 'second handoff is rejected by the per-root coordinator singleton'
    $resultPath = Join-Path $parentWaitCase.Directory 'result.json'
    Wait-Path $resultPath $null 12000 'Child did not report parent-exit timeout.'
    $result = [IO.File]::ReadAllText($resultPath) | ConvertFrom-Json
    Assert (-not $parent.HasExited) 'child enforces the parent-exit deadline before the coordinator finishes its delay'
    Assert ($result.status -eq 'handoff-failed' -and $result.message.Contains('coordinator did not exit')) 'child records the exact parent-exit timeout as a pre-setup failure'
    Assert (-not (Test-Path -LiteralPath $setupCountPath)) 'parent-exit timeout and duplicate request start no setup'
    Assert ((Get-FileHash -LiteralPath $updaterPath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceUpdaterHash) 'parent-exit timeout preserves installed updater bytes'
    Assert ((Get-FileHash -LiteralPath (Join-Path $installRoot 'INSTALL-MANIFEST.json') -Algorithm SHA256).Hash -eq $oldManifestHash) 'parent-exit timeout preserves the installed manifest'
    Assert ((Get-FileHash -LiteralPath (Join-Path $installRoot 'VideoProcessor.cfg') -Algorithm SHA256).Hash -eq $configHashBefore) 'parent-exit timeout preserves user config bytes'
    Assert ((Get-FileHash -LiteralPath (Join-Path $installRoot 'VideoProcessor.state') -Algorithm SHA256).Hash -eq $stateHashBefore) 'parent-exit timeout preserves user state bytes'
    Assert ($parent.WaitForExit(10000)) 'delayed coordinator eventually exits after the child rejects its timeout'
    Assert (Wait-NoStageProcess (Join-Path $parentWaitCase.Directory 'VideoProcessorUpdate.exe') 5000) 'parent-timeout child exits without lingering'
    $duplicate.Dispose(); $duplicate = $null
$player = $null
$playerPath = $null; $parent.Dispose(); $parent = $null

    $setupFailureCase = New-TestEnvelope 'setup-failed'
    $null = $stagedDirectories.Add($setupFailureCase.Directory)
    Reset-TestSignals
    $spec.envelopePath = $setupFailureCase.Path; $spec.handoffRoot = $setupFailureCase.Root
    $spec.checkpointPhase = ''; $spec.checkpointAction = ''; $spec.checkpointDelayMs = 0
    $spec.unattended = $true; $spec.delayAfterReadyMs = 0; $spec.setupExitCode = 42
    Write-Utf8NoBom $specPath ($spec | ConvertTo-Json -Depth 8 -Compress)
    $playerPath = Join-Path $installRoot 'VideoProcessor.exe'
    Copy-Item -LiteralPath $fixtureExe -Destination $playerPath
    $player = Start-Process -FilePath $playerPath -ArgumentList ('--fixture-player "' + $specPath + '"') -WorkingDirectory $installRoot -WindowStyle Hidden -PassThru
    Wait-Path $playerStartCountPath $player 10000 'Isolated fake VideoProcessor did not start.'
    Assert ([IO.File]::ReadAllText($playerStartCountPath).Trim() -eq '1') 'one isolated fake VideoProcessor starts before setup failure'
    $parent = Start-Process -FilePath $updaterPath -ArgumentList $argumentLine -WorkingDirectory $installRoot -WindowStyle Hidden -RedirectStandardError $stderrPath -PassThru
    Wait-Path $parentReadyPath $parent 20000 'Setup-failure case did not receive READY.'
    Wait-Path $setupObservationPath $null 15000 'Fake setup was not launched in the setup-failure case.'
    Assert ($parent.WaitForExit(10000)) 'coordinator exits while failed setup is handled by the staged child'
    $resultPath = Join-Path $setupFailureCase.Directory 'result.json'
    Wait-Path $resultPath $null 10000 'Setup failure did not produce a terminal result.'
    $result = [IO.File]::ReadAllText($resultPath) | ConvertFrom-Json
    Assert ($result.status -eq 'setup-failed') 'nonzero setup exit is recorded as setup-failed'
    Assert ([IO.File]::ReadAllText($setupCountPath).Trim() -eq '1') 'failed setup is attempted exactly once'
    Assert ((Get-FileHash -LiteralPath $updaterPath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $sourceUpdaterHash) 'failed setup leaves the installed updater untouched'
    Assert ((Get-FileHash -LiteralPath (Join-Path $installRoot 'INSTALL-MANIFEST.json') -Algorithm SHA256).Hash -eq $oldManifestHash) 'failed setup leaves the installed manifest untouched'
    Assert ((Get-FileHash -LiteralPath (Join-Path $installRoot 'VideoProcessor.cfg') -Algorithm SHA256).Hash -eq $configHashBefore) 'failed setup preserves user config bytes'
    Assert ((Get-FileHash -LiteralPath (Join-Path $installRoot 'VideoProcessor.state') -Algorithm SHA256).Hash -eq $stateHashBefore) 'failed setup preserves user state bytes'
    Assert ($player.WaitForExit(10000)) 'captured fake VideoProcessor closes before setup starts'
    Start-Sleep -Milliseconds 500
    Assert ([IO.File]::ReadAllText($playerStartCountPath).Trim() -eq '1') 'setup failure does not relaunch the captured VideoProcessor'
    $players = Get-ExactImageProcesses $playerPath 'VideoProcessor'
    Assert ($players.Count -eq 0) 'no fake VideoProcessor process remains after setup failure'
    foreach ($process in $players) { $process.Dispose() }
    Assert (Wait-NoStageProcess (Join-Path $setupFailureCase.Directory 'VideoProcessorUpdate.exe') 5000) 'setup-failure child exits without relaunching into the installed root'
    $player.Dispose()
    $parent.Dispose(); $parent = $null

    Reset-TestSignals
    $spec.envelopePath = $envelopePath; $spec.handoffRoot = $handoffRoot
    $spec.checkpointPhase = ''; $spec.checkpointAction = ''; $spec.checkpointDelayMs = 0
    $spec.unattended = $false; $spec.delayAfterReadyMs = 0; $spec.setupExitCode = 0
    Write-Utf8NoBom $specPath ($spec | ConvertTo-Json -Depth 8 -Compress)
    $parent = Start-Process -FilePath $updaterPath -ArgumentList $argumentLine -WorkingDirectory $installRoot -WindowStyle Hidden -RedirectStandardError $stderrPath -PassThru
    Wait-Path $parentReadyPath $parent 20000 'Updater parent did not finish the validated READY handshake.'
    Wait-Path $setupObservationPath $null 20000 'Staged child never replaced the installed updater.'
    Assert ($parent.WaitForExit(10000)) 'coordinator exits after its child reports READY'

    $observation = [IO.File]::ReadAllText($setupObservationPath) | ConvertFrom-Json
    Assert ($observation.setupCount -eq 1 -and [IO.File]::ReadAllText($setupCountPath).Trim() -eq '1') 'fake installer ran exactly once'
    Assert ($observation.childAliveAfterInstalledUpdaterReplacement) 'staged updater child stayed alive after replacing the installed executable'
    Assert ([IO.Path]::GetFullPath($observation.childImage).Equals([IO.Path]::GetFullPath((Join-Path $handoffDirectory 'VideoProcessorUpdate.exe')), [StringComparison]::OrdinalIgnoreCase)) 'child process image is the staged per-update copy'
    Assert ([IO.Path]::GetFullPath($observation.setupImage).Equals([IO.Path]::GetFullPath((Join-Path $handoffDirectory 'VideoProcessorSetup.exe')), [StringComparison]::OrdinalIgnoreCase)) 'installer process image is the staged fixture executable'
    Assert ((Get-FileHash -LiteralPath $updaterPath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $replacementUpdaterHash) 'installed updater bytes changed while only the staged updater was executing'
    Assert ((Get-FileHash -LiteralPath (Join-Path $installRoot 'INSTALL-MANIFEST.json') -Algorithm SHA256).Hash.ToLowerInvariant() -eq $newManifestHash) 'new signed installation manifest committed by the fixture setup'
    Assert ((Get-FileHash -LiteralPath (Join-Path $installRoot 'VideoProcessor.cfg') -Algorithm SHA256).Hash -eq $configHashBefore) 'user config bytes preserved'
    Assert ((Get-FileHash -LiteralPath (Join-Path $installRoot 'VideoProcessor.state') -Algorithm SHA256).Hash -eq $stateHashBefore) 'user state bytes preserved'

    $resultPath = Join-Path $handoffDirectory 'result.json'
    Wait-Path $resultPath $null 15000 'Staged updater did not write a terminal result.'
    $result = [IO.File]::ReadAllText($resultPath) | ConvertFrom-Json
    Assert ($result.status -eq 'complete') 'staged child verified the installed build and completed the handoff'

    $childProcesses = Get-ExactImageProcesses (Join-Path $handoffDirectory 'VideoProcessorUpdate.exe')
    $childWait = [Diagnostics.Stopwatch]::StartNew()
    while ($childProcesses.Count -ne 0 -and $childWait.ElapsedMilliseconds -lt 10000) {
        foreach ($process in $childProcesses) { $process.Dispose() }
        Start-Sleep -Milliseconds 50
        $childProcesses = Get-ExactImageProcesses (Join-Path $handoffDirectory 'VideoProcessorUpdate.exe')
    }
    Assert ($childProcesses.Count -eq 0) 'staged updater child exits before terminal cache cleanup'
    foreach ($process in $childProcesses) { $process.Dispose() }

    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $rootId = Hash-Bytes ([Text.UTF8Encoding]::new($false).GetBytes($sid + '|' + $installRoot.ToLowerInvariant()))
    $diagnosticCache = Join-Path (Join-Path $env:LOCALAPPDATA 'VideoProcessor\Updates') $rootId
    $diagnosticResult = Join-Path $diagnosticCache 'last-install-result.json'
    $diagnosticLog = Join-Path $diagnosticCache 'last-install-setup.log'
    Assert (-not (Test-Path -LiteralPath $diagnosticResult) -and -not (Test-Path -LiteralPath $diagnosticLog)) 'unique test root has no preexisting install diagnostics'
    $diagnosticFilesOwned = $true
    $cleaner = Start-Process -FilePath $fixtureExe -ArgumentList ('--fixture-cleanup "' + $specPath + '"') -WorkingDirectory $scratch -WindowStyle Hidden -RedirectStandardError (Join-Path $scratch 'cleanup.stderr') -PassThru
    Wait-Path (Join-Path $scratch 'cleanup-complete.txt') $cleaner 20000 'Fixture cleanup did not return.'
    Assert ($cleaner.WaitForExit(10000)) 'normal-startup handoff cleanup invocation exits'
    Assert (-not (Test-Path -LiteralPath $handoffDirectory)) 'successful terminal handoff stage is removed by normal cleanup'
    Assert ((Test-Path -LiteralPath $diagnosticResult) -and (Test-Path -LiteralPath $diagnosticLog)) 'result and setup log persist in the root-specific cache after stage cleanup'
    $savedResult = [IO.File]::ReadAllText($diagnosticResult) | ConvertFrom-Json
    Assert ($savedResult.status -eq 'complete') 'durable result retains the completed status'
    Assert ([IO.File]::ReadAllText($diagnosticLog).Contains('fixture setup completed')) 'durable setup log preserves fixture evidence'
    Write-Host "$checks staged updater handoff checks passed. Isolated fixture: $scratch"
}
finally {
    if ($null -eq $previousSpec) { Remove-Item Env:VP_HANDOFF_FIXTURE_SPEC -ErrorAction SilentlyContinue }
    else { $env:VP_HANDOFF_FIXTURE_SPEC = $previousSpec }
    if ($parent -and -not $parent.HasExited) {
        try { $parent.Kill(); $parent.WaitForExit(2000) } catch { }
    }
    if ($duplicate -and -not $duplicate.HasExited) {
        try { $duplicate.Kill(); $duplicate.WaitForExit(2000) } catch { }
    }
    if ($player -and -not $player.HasExited) {
        try { $player.Kill(); $player.WaitForExit(2000) } catch { }
    }
    if ($cleaner -and -not $cleaner.HasExited) {
        try { $cleaner.Kill(); $cleaner.WaitForExit(2000) } catch { }
    }
    if ($playerPath -and (Test-Path -LiteralPath $playerPath)) {
        foreach ($process in [Diagnostics.Process]::GetProcessesByName('VideoProcessor')) {
            try {
                if ([IO.Path]::GetFullPath($process.MainModule.FileName).Equals([IO.Path]::GetFullPath($playerPath), [StringComparison]::OrdinalIgnoreCase)) {
                    $process.Kill(); $process.WaitForExit(2000)
                }
            } catch { } finally { $process.Dispose() }
        }
    }
    foreach ($stageDirectory in $stagedDirectories) {
        if (-not $stageDirectory -or -not (Test-Path -LiteralPath $stageDirectory)) { continue }
        foreach ($path in @((Join-Path $stageDirectory 'VideoProcessorUpdate.exe'), (Join-Path $stageDirectory 'VideoProcessorSetup.exe'))) {
            foreach ($process in [Diagnostics.Process]::GetProcessesByName([IO.Path]::GetFileNameWithoutExtension($path))) {
                try {
                    if ([IO.Path]::GetFullPath($process.MainModule.FileName).Equals([IO.Path]::GetFullPath($path), [StringComparison]::OrdinalIgnoreCase)) {
                        $process.Kill(); $process.WaitForExit(2000)
                    }
                } catch { } finally { $process.Dispose() }
            }
        }
    }
    if ($rsa) { $rsa.Dispose() }
    if ($diagnosticFilesOwned -and $diagnosticCache) {
        $expectedParent = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'VideoProcessor\Updates')).TrimEnd('\') + '\'
        $fullDiagnosticCache = [IO.Path]::GetFullPath($diagnosticCache).TrimEnd('\')
        if ($fullDiagnosticCache.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $fullDiagnosticCache) -match '^[0-9a-f]{64}$') {
            foreach ($name in @('last-install-result.json', 'last-install-setup.log')) {
                $diagnosticPath = Join-Path $fullDiagnosticCache $name
                if (Test-Path -LiteralPath $diagnosticPath) { Remove-Item -LiteralPath $diagnosticPath -Force }
            }
            if ((Test-Path -LiteralPath $fullDiagnosticCache) -and @(Get-ChildItem -LiteralPath $fullDiagnosticCache -Force).Count -eq 0) {
                Remove-Item -LiteralPath $fullDiagnosticCache -Force
            }
        }
    }
    if ($testPreferencesOwned -and $testPreferencePath -and $testCache) {
        $expectedParent = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'VideoProcessor\Updates')).TrimEnd('\') + '\'
        $fullTestCache = [IO.Path]::GetFullPath($testCache).TrimEnd('\')
        if ($fullTestCache.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $fullTestCache) -match '^[0-9a-f]{64}$') {
            if (Test-Path -LiteralPath $testPreferencePath) { Remove-Item -LiteralPath $testPreferencePath -Force }
            if ((Test-Path -LiteralPath $fullTestCache) -and @(Get-ChildItem -LiteralPath $fullTestCache -Force).Count -eq 0) {
                Remove-Item -LiteralPath $fullTestCache -Force
            }
        }
    }
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    $fullScratch = [IO.Path]::GetFullPath($scratch).TrimEnd('\')
    if ($fullScratch.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $fullScratch) -match '^vp-update-handoff-[0-9a-f]{32}$' -and
        (Test-Path -LiteralPath $fullScratch)) {
        Remove-Item -LiteralPath $fullScratch -Recurse -Force
    }
}
