[CmdletBinding()]
param(
    [string]$BaseCommit = '982adb0ea309a7cf314ec2c933bd3213153ff649',
    [string]$BuildRoot = (Join-Path $PSScriptRoot '..\x64\Release'),
    [string]$DumpbinPath,
    [string]$MissingRuntimeProbe
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
if (-not $DumpbinPath) {
    $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'vswhere.exe was not found; provide -DumpbinPath.' }
    $DumpbinPath = & $vswhere -latest -products '*' -find 'VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe' | Select-Object -Last 1
}
if (-not $DumpbinPath -or -not (Test-Path -LiteralPath $DumpbinPath -PathType Leaf)) {
    throw 'Visual Studio dumpbin.exe was not found; provide -DumpbinPath.'
}

$gitRoot = (& git -C $repository rev-parse --show-toplevel).Trim()
if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($gitRoot) -ne $repository) {
    throw "Expected a Git checkout at $repository."
}
& git -C $repository cat-file -e "$BaseCommit^{commit}"
if ($LASTEXITCODE -ne 0) { throw "Base commit is unavailable: $BaseCommit" }

# This allowlist is deliberately narrow. A change outside the Magewell integration
# surface needs a review of rendering and existing capture behavior before release.
$allowed = @(
    '^\.gitignore$',
    '^docs/MAGEWELL_TESTING\.md$',
    '^tools/test_magewell_isolation\.ps1$',
    '^tools/magewell_runtime_probe\.cpp$',
    '^tools/MagewellRuntimeProbe\.vcxproj$',
    '^src/VideoProcessor-Lib/magewell/',
    '^src/VideoProcessor-Lib/Magewell[^/]*\.(cpp|h)$',
    '^src/VideoProcessor-Lib/ConfigurationDiscovery\.(cpp|h)$',
    '^src/VideoProcessor-Lib/VideoProcessor-Lib\.vcxproj(?:\.filters)?$',
    '^src/VideoProcessor-GUI/VideoProcessorDlg\.(cpp|h)$',
    '^src/VideoProcessor-GUI/VideoProcessor-GUI\.vcxproj(?:\.filters)?$',
    '^src/VideoProcessor-ConfigDiscovery/VideoProcessor-ConfigDiscovery\.vcxproj(?:\.filters)?$',
    '^src/VideoProcessor-Test/Magewell[^/]*\.(cpp|h)$',
    '^src/VideoProcessor-Test/VideoProcessor-Test\.vcxproj\.vcxproj(?:\.filters)?$'
)
$changed = @(& git -C $repository diff --name-only $BaseCommit --)
if ($LASTEXITCODE -ne 0) { throw 'Cannot compare source against pinned beta commit.' }
$untracked = @(& git -C $repository ls-files --others --exclude-standard)
if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate untracked source files.' }
$changed = @($changed + $untracked | Where-Object { $_ } | Sort-Object -Unique)
$outside = @($changed | Where-Object {
    $path = $_.Replace('\', '/')
    -not @($allowed | Where-Object { $path -match $_ }).Count
})
if ($outside.Count) {
    throw "Source isolation failed: changes outside reviewed Magewell integration surface:`n$($outside -join "`n")"
}
Write-Host "PASS: $($changed.Count) changed source paths stay within the Magewell integration surface from $BaseCommit."

$artifacts = @(
    'VideoProcessor-GUI.exe',
    'VideoProcessorConfig.exe',
    'VideoProcessorConfigDiscovery.dll'
)
$bundledMagewell = @(Get-ChildItem -LiteralPath $BuildRoot -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '(?i)^LibMWCapture(?:64)?\.dll$' })
if ($bundledMagewell.Count) {
    throw "Build output bundles the Magewell runtime: $($bundledMagewell.FullName -join ', ')"
}
foreach ($relative in $artifacts) {
    $artifact = Join-Path $BuildRoot $relative
    if (-not (Test-Path -LiteralPath $artifact -PathType Leaf)) { throw "Missing x64 Release artifact: $artifact" }
    $headers = @(& $DumpbinPath /nologo /headers $artifact)
    if ($LASTEXITCODE -ne 0 -or -not ($headers -match 'machine \(x64\)')) {
        throw "Artifact is not a readable x64 PE: $artifact"
    }
    $imports = @(& $DumpbinPath /nologo /imports $artifact)
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect PE imports: $artifact" }
    if ($imports -match '(?i)\bLibMWCapture(?:64)?\.dll\b') {
        throw "Magewell runtime is imported by $artifact; absent SDK could prevent startup."
    }
    $sha256 = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash
    Write-Host "PASS: x64 PE has no Magewell load-time or delay imports: $relative SHA256=$sha256"
}

if ($MissingRuntimeProbe) {
    $probe = [IO.Path]::GetFullPath($MissingRuntimeProbe)
    if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) { throw "Missing runtime probe: $probe" }
    $output = @(& $probe --expect-unavailable 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -or -not ($output -match '^MAGEWELL_UNAVAILABLE: .+')) {
        throw "Missing-runtime probe failed (exit $exitCode):`n$($output -join "`n")"
    }
    Write-Host 'PASS: production Magewell acquisition reports unavailable without an installed runtime.'
} else {
    Write-Warning 'Missing-runtime execution was not checked. Supply -MissingRuntimeProbe for the full isolation gate.'
}
