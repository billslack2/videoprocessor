[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$LogPath,
    [ValidateRange(1, 2147483647)][int]$FromLine = 1,
    [ValidateRange(1, 2147483647)][int]$ToLine = 2147483647,
    [ValidateRange(0, 2147483647)][int]$MinimumSourceUpdates = 0,
    [switch]$ExpectNoDiskReads
)
$ErrorActionPreference = 'Stop'
if ($ToLine -lt $FromLine) { throw 'ToLine must be at least FromLine.' }
$all = @(Get-Content -LiteralPath $LogPath)
$last = [Math]::Min($all.Count, $ToLine)
if ($FromLine -gt $last) { throw 'Selected interval is empty.' }
$lines = @($all[($FromLine - 1)..($last - 1)])
function Count-Matches([string]$pattern) {
    return @($lines | Where-Object { $_ -match $pattern }).Count
}
$result = [ordered]@{
    LogPath = (Resolve-Path -LiteralPath $LogPath).Path
    FromLine = $FromLine
    ToLine = $last
    DiskReads = Count-Matches 'Configuration disk read:'
    SourceSnapshotReuses = Count-Matches 'Renderer source configuration reused:'
    HdrAbsentUpdates = Count-Matches 'Renderer source configuration reused:.*hdr=0'
    HdrPresentUpdates = Count-Matches 'Renderer source configuration reused:.*hdr=1'
    SnapshotAcceptances = Count-Matches 'Renderer configuration snapshot accepted:'
    MetadataOnlyCaptureUpdates = Count-Matches 'Capture video state metadata-only update:'
    LegacyGeneralWarnings = Count-Matches 'built-in renderer policy keys in \[general\] are deprecated'
    RendererRebuildRequests = Count-Matches 'display: effective settings changed.*requesting renderer rebuild'
}
$result | ConvertTo-Json
if ($result.SourceSnapshotReuses -lt $MinimumSourceUpdates) {
    throw "Insufficient source events: expected at least $MinimumSourceUpdates. An idle session does not prove the fix."
}
if ($ExpectNoDiskReads -and $result.DiskReads -ne 0) {
    throw "Unexpected disk reads in selected interval: $($result.DiskReads). Exclude startup and deliberate configuration reloads."
}
