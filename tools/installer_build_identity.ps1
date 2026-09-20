Set-StrictMode -Version Latest
function Get-VpSourceIdentity([string]$Root) {
    $commit = (& git -C $Root rev-parse HEAD) -join ''
    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[a-f0-9]{40}$') { throw 'A Git source checkout is required.' }
    $status = (& git -C $Root status --porcelain=v1 --untracked-files=all -z) -join [Environment]::NewLine
    if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect source status.' }
    $listing = (& git -C $Root ls-files --cached --others --exclude-standard -z) -join [Environment]::NewLine
    if ($LASTEXITCODE -ne 0) { throw 'Unable to inventory source files.' }
    [string[]]$files = $listing.Split(@([char]0), [StringSplitOptions]::RemoveEmptyEntries)
    [Array]::Sort($files, [StringComparer]::Ordinal)
    $snapshot = [Text.StringBuilder]::new()
    $null = $snapshot.Append($commit).Append([char]0)
    foreach ($relative in $files) {
        $file = Join-Path $Root $relative
        $hash = if (Test-Path -LiteralPath $file -PathType Leaf) {
            (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
        } elseif (Test-Path -LiteralPath $file) {
            throw "Nested source repositories are not supported by installer stamping: $relative"
        } else { 'deleted' }
        $null = $snapshot.Append($relative).Append([char]0).Append($hash).Append([char]0)
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $fingerprint = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($snapshot.ToString())))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
    $dirty = -not [string]::IsNullOrEmpty($status)
    $label = $commit.Substring(0,12)
    $build = $commit
    if ($dirty) { $label += '-dirty-' + $fingerprint.Substring(0,12); $build += '-dirty-' + $fingerprint }
    return [pscustomobject]@{ commit=$commit; dirty=$dirty; fingerprint=$fingerprint; label=$label; build=$build }
}
function Get-VpInstallerBaseName([string]$CoreVersion, $Identity) {
    $name = 'VideoProcessorSetup-' + $CoreVersion
    if ($Identity.dirty) { $name += '-' + $Identity.label }
    return $name
}

