<#
.SYNOPSIS
Fetches the pinned VP Player runtime kit and installs it beside VideoProcessor.exe.

.DESCRIPTION
The Player input is a source plugin (plugins\sources\vp_player.dll) that drives
its own mpv build (plugins\sources\mpv\). Neither is built by this solution:
mpv is built from the anevard/mpv fork (its vpsink video output is part of mpv),
so both ship together as one kit, published as a release asset. This script
downloads that kit, checks its SHA-256 against the value pinned below, verifies
every file against the kit's own manifest, and copies the two pieces into the
destination. Without the kit VP runs as before and no Player input appears.

Moving to a newer kit is a change to the two pinned values below.

.PARAMETER Destination
The folder holding VideoProcessor.exe. Defaults to x64\Release beside this
repository (the x64 Release build output).

.PARAMETER ZipPath
Use an already downloaded kit zip instead of fetching it. The same SHA-256
check applies.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File tools\fetch_player_kit.ps1
.EXAMPLE
powershell -ExecutionPolicy Bypass -File tools\fetch_player_kit.ps1 -Destination C:\VideoProcessor
#>
[CmdletBinding()]
param(
    [string]$Destination = (Join-Path (Split-Path -Parent $PSScriptRoot) 'x64\Release'),
    [string]$ZipPath
)

$ErrorActionPreference = 'Stop'

# The pinned kit. Both values change together when a new kit is published.
$KitUrl = 'https://github.com/anevard/mpv/releases/download/player-kit-2026-11-01/vp-player-kit-x64.zip'
$KitSha256 = 'C5640A93F3F7690E53AD8973995AC74605DED8517070C2C43CD9F2740EA3D630'

function Get-Sha256([string]$path) {
    (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToUpperInvariant()
}

$Destination = [IO.Path]::GetFullPath($Destination)
if (-not (Test-Path -LiteralPath (Join-Path $Destination 'VideoProcessor.exe'))) {
    throw "No VideoProcessor.exe in '$Destination'. Pass -Destination <folder holding VideoProcessor.exe>."
}

$work = Join-Path ([IO.Path]::GetTempPath()) ("vp-player-kit-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    if ($ZipPath) {
        $zip = [IO.Path]::GetFullPath($ZipPath)
        Write-Host "Using $zip"
    } else {
        $zip = Join-Path $work 'vp-player-kit-x64.zip'
        Write-Host "Downloading $KitUrl"
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $KitUrl -OutFile $zip -UseBasicParsing
    }

    $actual = Get-Sha256 $zip
    if ($actual -ne $KitSha256) {
        throw "SHA-256 mismatch for the kit zip: expected $KitSha256, got $actual. Nothing was installed."
    }
    Write-Host "SHA-256 OK: $actual"

    $unpacked = Join-Path $work 'unpacked'
    Expand-Archive -LiteralPath $zip -DestinationPath $unpacked
    $sources = Join-Path $unpacked 'plugins\sources'
    foreach ($required in @('vp_player.dll', 'mpv\mpv.exe', 'mpv\MANIFEST.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $sources $required))) {
            throw "The kit zip has no plugins\sources\$required. Nothing was installed."
        }
    }

    # Every file in the kit against the kit's own manifest (lines "  <sha256> *./path").
    $listed = 0
    foreach ($line in Get-Content -LiteralPath (Join-Path $sources 'mpv\MANIFEST.txt')) {
        if ($line -notmatch '^\s+([0-9a-f]{64}) \*\./(.+)$') { continue }
        $listed++
        $file = Join-Path $sources ($Matches[2] -replace '/', '\')
        if (-not (Test-Path -LiteralPath $file)) { throw "Manifest names a missing file: $($Matches[2])" }
        if ((Get-Sha256 $file) -ne $Matches[1].ToUpperInvariant()) { throw "Manifest hash mismatch: $($Matches[2])" }
    }
    $present = @(Get-ChildItem -LiteralPath $sources -Recurse -File | Where-Object { $_.Name -ne 'MANIFEST.txt' }).Count
    if ($listed -ne $present) { throw "Manifest lists $listed files, the kit holds $present. Nothing was installed." }
    Write-Host "Manifest OK: $listed files"

    # Install: the plugin file and the whole mpv folder, replacing any earlier kit.
    $target = Join-Path $Destination 'plugins\sources'
    New-Item -ItemType Directory -Path $target -Force | Out-Null
    $oldMpv = Join-Path $target 'mpv'
    if (Test-Path -LiteralPath $oldMpv) { Remove-Item -LiteralPath $oldMpv -Recurse -Force }
    Copy-Item -LiteralPath (Join-Path $sources 'mpv') -Destination $oldMpv -Recurse
    Copy-Item -LiteralPath (Join-Path $sources 'vp_player.dll') -Destination (Join-Path $target 'vp_player.dll') -Force

    Write-Host "Installed into $target"
    Get-Content -LiteralPath (Join-Path $target 'mpv\MANIFEST.txt') |
        Where-Object { $_ -match '^(vp_player\.dll|mpv|libplacebo pin):' } |
        ForEach-Object { Write-Host "  $_" }
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
