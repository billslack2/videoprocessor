# Shared by release packaging, setup, and prerequisite regression checks.
function Get-VpFileVersion {
    param([Parameter(Mandatory = $true)][string]$FilePath)
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($FilePath)
    return [version]::new($info.FileMajorPart, $info.FileMinorPart,
        $info.FileBuildPart, $info.FilePrivatePart)
}

function Get-VpRuntimeProblems {
    param(
        [Parameter(Mandatory = $true)][string]$Directory,
        [Parameter(Mandatory = $true)][string[]]$RuntimeFiles,
        [Parameter(Mandatory = $true)][version]$MinimumVersion,
        [switch]$ExistingOnly
    )
    foreach ($name in $RuntimeFiles) {
        $file = Join-Path $Directory $name
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            if (-not $ExistingOnly) { "Missing: $file" }
            continue
        }
        $version = Get-VpFileVersion $file
        if ($version -lt $MinimumVersion) {
            "Too old: $file ($version; required $MinimumVersion or newer)"
        }
    }
}

function Assert-VpRedistributable {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][version]$MinimumVersion,
        [string]$ExpectedSha256
    )
    if (-not (Test-Path -LiteralPath $FilePath -PathType Leaf)) {
        throw "Missing Microsoft x64 runtime installer: $FilePath"
    }
    if ($ExpectedSha256 -and
        (Get-FileHash -LiteralPath $FilePath -Algorithm SHA256).Hash -ne $ExpectedSha256) {
        throw 'The runtime installer does not match this release. Extract the complete ZIP again.'
    }
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($FilePath)
    if ($info.OriginalFilename -ine 'VC_redist.x64.exe' -or
        $info.ProductName -notmatch 'Microsoft Visual C\+\+.*\(x64\)') {
        throw 'An official Microsoft Visual C++ x64 Redistributable installer is required.'
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $FilePath
    if ($signature.Status -ne 'Valid' -or
        $signature.SignerCertificate.Subject -notmatch '(^|, )O=Microsoft Corporation(,|$)') {
        throw 'The runtime installer must have a valid Microsoft Authenticode signature.'
    }
    $version = Get-VpFileVersion $FilePath
    if ($version -lt $MinimumVersion) {
        throw "Runtime installer $version is older than the required $MinimumVersion."
    }
    return $version
}
