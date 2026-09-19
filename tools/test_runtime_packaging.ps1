[CmdletBinding()]
param(
    [string]$BuildRoot,
    [Parameter(Mandatory = $true)][string]$VcRedistPath,
    [string]$OldRuntimeDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repositoryRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildRoot) { $BuildRoot = Join-Path $repositoryRoot 'x64\Release' }
. (Join-Path $repositoryRoot 'packaging\prerequisites\setup-runtime.ps1')
. (Join-Path $PSScriptRoot 'runtime_packaging.ps1')
$fixtureRoot = Join-Path $repositoryRoot ('artifacts\runtime-tests-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $fixtureRoot
$script:checks = 0
function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
    Write-Host "PASS $Message"
}
function Assert-Throws([scriptblock]$Action, [string]$Pattern, [string]$Message) {
    $caught = $false
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        $caught = $true
    }
    Assert-True $caught $Message
}

$minimum = [version]'14.44.35211.0'
$installerVersion = Assert-VpRedistributable $VcRedistPath $minimum
Assert-True ($installerVersion -ge $minimum) 'official signed x64 installer meets policy'
Assert-Throws { Assert-VpRedistributable $VcRedistPath ([version]'99.0.0.0') } 'older than' 'outdated installer rejected'
Assert-Throws { Assert-VpRedistributable $VcRedistPath $minimum ('0' * 64) } 'does not match' 'installer hash mismatch rejected'
Assert-Throws { Assert-VpRedistributable (Join-Path $fixtureRoot 'missing.exe') $minimum } 'Missing' 'missing installer rejected'
Assert-Throws { Assert-VpRedistributable (Join-Path $BuildRoot 'VideoProcessorConfig.exe') $minimum } 'official Microsoft' 'application cannot masquerade as runtime installer'

$binary = Join-Path $fixtureRoot 'VideoProcessorConfig.exe'
Copy-Item -LiteralPath (Join-Path $BuildRoot 'VideoProcessorConfig.exe') -Destination $binary
$recordPath = $binary + '.runtime.json'
Copy-Item -LiteralPath ((Join-Path $BuildRoot 'VideoProcessorConfig.exe') + '.runtime.json') -Destination $recordPath
$originalRecord = Get-Content -LiteralPath $recordPath -Raw
$record = $originalRecord | ConvertFrom-Json
$policy = [pscustomobject]@{
    minimumVersion = $minimum.ToString()
    buildArtifacts = @('config/VideoProcessorConfig.exe')
    runtimeFiles = @('msvcp140.dll')
}
$copyPlan = @([pscustomobject]@{ Source = $binary; RelativeDestination = 'config\VideoProcessorConfig.exe' })
$requirement = Get-VpPackageRuntimeRequirement $copyPlan $policy $VcRedistPath
Assert-True ([version]$requirement.minimumVersion -ge $minimum) 'package minimum includes Config rather than just the host'
$record.toolsetVersion = '99.0.0'
$record | ConvertTo-Json | Set-Content -LiteralPath $recordPath -Encoding UTF8
Assert-Throws { Get-VpPackageRuntimeRequirement $copyPlan $policy $VcRedistPath } 'older than' 'new compiler toolset cannot outgrow the included installer silently'
Set-Content -LiteralPath $recordPath -Value $originalRecord -Encoding UTF8
$record = $originalRecord | ConvertFrom-Json
$record.sha256 = '0' * 64
$record | ConvertTo-Json | Set-Content -LiteralPath $recordPath -Encoding UTF8
Assert-Throws { Get-VpPackageRuntimeRequirement $copyPlan $policy $VcRedistPath } 'Stale or invalid' 'binary/build-record mismatch rejected'
Set-Content -LiteralPath $recordPath -Value $originalRecord -Encoding UTF8
$absentPlan = @([pscustomobject]@{ Source = (Join-Path $fixtureRoot 'unrecorded.exe'); RelativeDestination = 'config\VideoProcessorConfig.exe' })
Assert-Throws { Get-VpPackageRuntimeRequirement $absentPlan $policy $VcRedistPath } 'Missing build runtime record' 'missing compiler record rejected'

# A copied PE is only inspected, never executed: model a newer vendor DLL family.
$vendor = Join-Path $fixtureRoot 'vendor.dll'
$bytes = [IO.File]::ReadAllBytes($binary)
$offset = [BitConverter]::ToInt32($bytes, 0x3c)
$bytes[$offset + 26] = 99
[IO.File]::WriteAllBytes($vendor, $bytes)
$newerPlan = $copyPlan + [pscustomobject]@{ Source = $vendor; RelativeDestination = 'config\vendor.dll' }
Assert-Throws { Get-VpPackageRuntimeRequirement $newerPlan $policy $VcRedistPath } 'older than' 'newer third-party linker family is included'
$bytes[$offset + 4] = 0x4c
$bytes[$offset + 5] = 0x01
[IO.File]::WriteAllBytes($vendor, $bytes)
Assert-Throws { Get-VpPeLinkerVersion $vendor } 'not x64' 'x86 payload rejected'

$missing = @(Get-VpRuntimeProblems $fixtureRoot @('missing-runtime.dll') $minimum)
Assert-True ($missing.Count -eq 1) 'missing system runtime reported'
$missing = @(Get-VpRuntimeProblems $fixtureRoot @('missing-runtime.dll') $minimum -ExistingOnly)
Assert-True ($missing.Count -eq 0) 'absent private DLL is allowed'
if ($OldRuntimeDirectory) {
    $old = @(Get-VpRuntimeProblems $OldRuntimeDirectory @('msvcp140.dll') $minimum)
    Assert-True ($old.Count -eq 1 -and $old[0] -match 'Too old') 'real older MSVCP140 rejected'
}

# Exercise setup decisions without installing or modifying the system runtime.
$prerequisites = Join-Path $fixtureRoot 'prerequisites'
$null = New-Item -ItemType Directory -Path $prerequisites
Copy-Item -LiteralPath $VcRedistPath -Destination (Join-Path $prerequisites 'vc_redist.x64.exe')
$requirement | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $prerequisites 'runtime-requirement.json') -Encoding UTF8
$sentinel = Join-Path $fixtureRoot 'VideoProcessor.cfg'
Set-Content -LiteralPath $sentinel -Value '# user configuration; must remain byte-identical' -Encoding UTF8
$before = (Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
$script:systemChecks = 0
$script:installerCalls = 0
$script:scenario = 'ready'
$script:installerExit = 0
function Get-VpRuntimeProblems {
    param($Directory, $RuntimeFiles, $MinimumVersion, [switch]$ExistingOnly)
    if ($ExistingOnly) {
        if ($script:scenario -eq 'private') { 'Too old: private runtime DLL' }
        return
    }
    $script:systemChecks++
    if ($script:scenario -eq 'missing' -or
        ($script:scenario -eq 'install' -and $script:systemChecks -eq 1)) {
        'Too old: system runtime DLL'
    }
}
function Start-Process {
    param($FilePath, $Verb, $ArgumentList, [switch]$Wait, [switch]$PassThru)
    $script:installerCalls++
    if ($script:installerExit -eq 1223) { throw 'The operation was canceled by the user.' }
    if ($Verb -ne 'RunAs' -or $ArgumentList -notcontains '/norestart' -or
        $ArgumentList -notcontains '/install' -or -not $Wait) { throw 'Unsafe installer invocation' }
    [pscustomobject]@{ ExitCode = $script:installerExit }
}
try {
    $result = Invoke-VpRuntimeSetup -PrerequisiteDirectory $prerequisites
    Assert-True ($result -eq 0 -and $script:installerCalls -eq 0) 'sufficient runtime skips installer'
    $script:scenario = 'missing'
    $result = Invoke-VpRuntimeSetup -PrerequisiteDirectory $prerequisites -CheckOnly
    Assert-True ($result -eq 2 -and $script:installerCalls -eq 0) 'check-only reports old runtime without installation'
    $script:scenario = 'private'
    $result = Invoke-VpRuntimeSetup -PrerequisiteDirectory $prerequisites
    Assert-True ($result -eq 1 -and $script:installerCalls -eq 0) 'old private runtime blocks setup without deleting DLLs'
    $script:scenario = 'install'; $script:systemChecks = 0
    $result = Invoke-VpRuntimeSetup -PrerequisiteDirectory $prerequisites
    Assert-True ($result -eq 0 -and $script:installerCalls -eq 1 -and $script:systemChecks -eq 2) 'old runtime installs with elevation/no restart and rechecks files'
    $script:systemChecks = 0; $script:installerExit = 3010
    $result = Invoke-VpRuntimeSetup -PrerequisiteDirectory $prerequisites
    Assert-True ($result -eq 3010) 'reboot-required result is preserved'
    $script:systemChecks = 0; $script:installerExit = 1603
    $result = Invoke-VpRuntimeSetup -PrerequisiteDirectory $prerequisites
    Assert-True ($result -eq 1) 'installer failure blocks success'
    $script:systemChecks = 0; $script:installerExit = 1223
    $result = Invoke-VpRuntimeSetup -PrerequisiteDirectory $prerequisites
    Assert-True ($result -eq 1) 'UAC cancellation reports failure without claiming readiness'
    $script:systemChecks = 0; $script:installerExit = 1638
    $result = Invoke-VpRuntimeSetup -PrerequisiteDirectory $prerequisites
    Assert-True ($result -eq 0) 'concurrent newer install accepted only after successful recheck'
    $script:scenario = 'missing'; $script:installerExit = 0
    $result = Invoke-VpRuntimeSetup -PrerequisiteDirectory $prerequisites
    Assert-True ($result -eq 1) 'nominal installer success cannot hide unsatisfied runtime'
    $after = (Get-FileHash -LiteralPath $sentinel -Algorithm SHA256).Hash
    Assert-True ($before -eq $after) 'setup leaves user configuration byte-identical'
} finally {
    Remove-Item Function:\Start-Process
    . (Join-Path $repositoryRoot 'packaging\prerequisites\runtime-common.ps1')
}
Write-Host "Passed $script:checks prerequisite regression checks. Fixtures: $fixtureRoot"
