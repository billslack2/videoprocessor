[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string[]]$InstallerPaths,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$ReleaseTag,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{40}$')][string]$CertificateThumbprint,
    [Parameter(Mandatory=$true)][string]$NotesPath,
    [string]$OutputPath,
    [ValidateRange(1,365)][int]$ValidDays=90
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'installer_build_identity.ps1')
$source=Get-VpSourceIdentity $root
if ($source.dirty) { throw 'Update releases must come from a clean committed source tree.' }
$notes=Get-Content -LiteralPath $NotesPath -Raw
if ($notes.Length -gt 30000) { throw 'Release notes exceed 30000 characters.' }
$packages=@(); $baseline=$null; $flavors=@{}
foreach ($path in $InstallerPaths) {
    $file=(Resolve-Path -LiteralPath $path).Path
    $manifestFile=$file+'.manifest.json'
    $manifest=Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
    if ($manifest.dirty -or $manifest.sourceCommit -ne $source.commit -or $manifest.sourceFingerprint -ne $source.fingerprint -or $manifest.updateSequence -le 0) { throw 'Installer does not identify this exact clean release source and a positive release sequence.' }
    if ($manifest.flavor -notin @('full','config') -or $flavors.ContainsKey($manifest.flavor)) { throw 'Duplicate or unknown package flavor.' }
    $expectedId = if ($manifest.flavor -eq 'full') { 'VideoProcessor-42D852F1-70E9-43ED-8739-D61752106D59' } else { 'VideoProcessorConfig-BA15DBE8-210F-42AA-AE86-B4628395E77F' }
    if ($manifest.schemaVersion -ne 1 -or $manifest.applicationId -ne $expectedId -or $manifest.updateChannel -notin @('stable','beta') -or $manifest.rpcVersion -le 0 -or $manifest.configurationVersion -le 0) { throw 'Unsupported installer identity or release metadata.' }
    $flavors[$manifest.flavor]=$true
    if ($baseline) {
        foreach ($property in @('updateSequence','updateChannel','sourceCommit','sourceFingerprint','coreVersion','rpcVersion','configurationVersion')) {
            if ($manifest.$property -ne $baseline.$property) { throw "Packages disagree on $property." }
        }
    } else { $baseline=$manifest }
    $hash=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    $sidecar=(Get-Content -LiteralPath ($file+'.sha256') -Raw).Trim().Split(' ')[0]
    if ($hash -ne $sidecar) { throw 'Installer changed after packaging.' }
    $packages += [ordered]@{flavor=$manifest.flavor;url=('https://github.com/billslack2/videoprocessor/releases/download/'+$ReleaseTag+'/'+[IO.Path]::GetFileName($file));size=(Get-Item -LiteralPath $file).Length;sha256=$hash;installManifestSha256=(Get-FileHash -LiteralPath $manifestFile -Algorithm SHA256).Hash.ToLowerInvariant()}
}
if (-not $baseline -or $packages.Count -gt 2) { throw 'Supply the full and/or Config-only installer.' }
$payload=[ordered]@{schemaVersion=1;minimumUpdaterVersion=1;sequence=$baseline.updateSequence;version=$baseline.coreVersion;commit=$baseline.sourceCommit;channel=$baseline.updateChannel;architecture='x64';rpcVersion=$baseline.rpcVersion;configurationVersion=$baseline.configurationVersion;expiresUtc=[DateTime]::UtcNow.AddDays($ValidDays).ToString('o');notes=$notes;packages=$packages}
$bytes=[Text.UTF8Encoding]::new($false).GetBytes(($payload|ConvertTo-Json -Depth 8 -Compress))
$certificate=Get-Item -LiteralPath ('Cert:\CurrentUser\My\'+$CertificateThumbprint)
$private=[Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($certificate)
if (-not $private) { throw 'Signing certificate has no private key available to this account.' }
try {
    $trusted=[IO.File]::ReadAllText((Join-Path $root 'packaging\updates\UpdatePublicKey.xml')).Trim()
    if ($private.ToXmlString($false) -ne $trusted) { throw 'Signing key does not match the public key embedded in the updater.' }
    $signature=$private.SignData($bytes,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
} finally { $private.Dispose() }
if (-not $OutputPath) { $OutputPath=Join-Path $root 'artifacts\installers\vp-update.json' }
[IO.File]::WriteAllText($OutputPath,([ordered]@{payload=[Convert]::ToBase64String($bytes);signature=[Convert]::ToBase64String($signature)}|ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
Write-Host "Signed descriptor: $OutputPath"
Write-Host 'Upload this descriptor and the exact setup files to the matching draft GitHub release. Publish only after qualification. This command does not publish.'
