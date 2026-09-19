[CmdletBinding()]
param([switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'runtime-common.ps1')

function Invoke-VpRuntimeSetup {
param(
    [switch]$CheckOnly,
    [string]$PrerequisiteDirectory = $PSScriptRoot
)
try {
    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'VideoProcessor requires a 64-bit Windows installation.'
    }
    $requirement = Get-Content -LiteralPath (Join-Path $PrerequisiteDirectory 'runtime-requirement.json') -Raw | ConvertFrom-Json
    if ($requirement.schemaVersion -ne 1 -or $requirement.architecture -ne 'x64') {
        throw 'Unsupported runtime requirement file. Extract the complete ZIP again.'
    }
    $minimum = [version]$requirement.minimumVersion
    $packageRoot = Split-Path -Parent $PrerequisiteDirectory
    # Do not silently delete a user's DLLs. An old private copy can shadow System32.
    $privateProblems = @(foreach ($directory in @($packageRoot,
        (Join-Path $packageRoot 'config'), (Join-Path $packageRoot 'vprenderer'))) {
        Get-VpRuntimeProblems -Directory $directory -RuntimeFiles $requirement.runtimeFiles `
            -MinimumVersion $minimum -ExistingOnly
    })
    if ($privateProblems.Count) {
        $privateProblems | ForEach-Object { Write-Host $_ }
        throw 'Old Microsoft runtime DLLs were found in the VP folder. Extract this release into a clean folder, preserve your configuration, and run setup there.'
    }
    # A 32-bit PowerShell process must inspect the native x64 system files.
    $systemDirectory = if ([Environment]::Is64BitProcess) {
        [Environment]::SystemDirectory
    } else { Join-Path $env:WINDIR 'Sysnative' }
    $problems = @(Get-VpRuntimeProblems -Directory $systemDirectory `
        -RuntimeFiles $requirement.runtimeFiles -MinimumVersion $minimum)
    if (-not $problems.Count) {
        Write-Host "Microsoft Visual C++ x64 runtime $minimum or newer is ready."
        Write-Host 'You can open VideoProcessor.exe or config\VideoProcessorConfig.exe.'
        return 0
    }
    Write-Host "This release requires Microsoft Visual C++ x64 runtime $minimum or newer."
    $problems | ForEach-Object { Write-Host $_ }
    if ($CheckOnly) { return 2 }

    $installer = Join-Path $PrerequisiteDirectory 'vc_redist.x64.exe'
    $null = Assert-VpRedistributable -FilePath $installer -MinimumVersion $minimum `
        -ExpectedSha256 $requirement.installerSha256
    Write-Host 'Installing the included Microsoft runtime. Approve the Windows administrator prompt.'
    $process = Start-Process -FilePath $installer -Verb RunAs `
        -ArgumentList '/install', '/passive', '/norestart' -Wait -PassThru
    if ($process.ExitCode -eq 3010) {
        Write-Host 'Installation succeeded. Restart Windows, then run SETUP-RUNTIME.cmd again before opening VP or Config.'
        return 3010
    }
    # 1638 can occur when another process installs a newer runtime during this check.
    if ($process.ExitCode -notin @(0, 1638)) {
        throw "Microsoft runtime setup failed (exit $($process.ExitCode)). See the Microsoft installer log in your TEMP folder."
    }
    $remaining = @(Get-VpRuntimeProblems -Directory $systemDirectory `
        -RuntimeFiles $requirement.runtimeFiles -MinimumVersion $minimum)
    if ($remaining.Count) {
        $remaining | ForEach-Object { Write-Host $_ }
        throw 'The required runtime is still unavailable. Restart Windows and retry; use the included installer Repair option if necessary.'
    }
    Write-Host 'Runtime setup completed. You can now open VP or its Config editor.'
    return 0
} catch {
    Write-Host "Runtime setup could not complete: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'Official runtime help: https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist'
    return 1
}

}
if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-VpRuntimeSetup -CheckOnly:$CheckOnly)
}
