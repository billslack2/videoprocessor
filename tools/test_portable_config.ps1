[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ZipPath,
    [string]$ExpectedConfigPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'VideoProcessor.cfg')
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem
$expected=(Get-FileHash -LiteralPath $ExpectedConfigPath -Algorithm SHA256).Hash
$zip=[IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $ZipPath).Path)
try {
    $samples=@($zip.Entries | Where-Object { $_.FullName -ieq 'VideoProcessor.cfg.example' })
    if($samples.Count -ne 1 -or $samples[0].FullName -cne 'VideoProcessor.cfg.example' -or $samples[0].Length -eq 0){
        throw 'Portable ZIP must contain one nonempty root VideoProcessor.cfg.example.'
    }
    if(@($zip.Entries | Where-Object { $_.FullName -ieq 'VideoProcessor.cfg' }).Count){
        throw 'Portable ZIP must not contain an active VideoProcessor.cfg that could overwrite user settings.'
    }
    $stream=$samples[0].Open()
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $actual=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
    finally { $sha.Dispose(); $stream.Dispose() }
    if($actual -ne $expected){throw 'Portable sample differs from the source configuration.'}
    $entries=@($zip.Entries | Where-Object { $_.FullName -ceq 'INSTALL-MANIFEST.json' })
    if($entries.Count -ne 1){throw 'Portable ZIP must contain one root install manifest.'}
    $reader=[IO.StreamReader]::new($entries[0].Open())
    try { $manifest=$reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    $records=@($manifest.files | Where-Object { $_.path -ceq 'VideoProcessor.cfg.example' })
    if($records.Count -ne 1 -or $records[0].policy -ne 'seed' -or $records[0].sha256 -ne $expected){
        throw 'Portable manifest must preserve the sample as a seed with its source SHA-256.'
    }
    Write-Host "PASS portable sample: VideoProcessor.cfg.example ($($samples[0].Length) bytes), source/manifest SHA256=$expected"
} finally { $zip.Dispose() }