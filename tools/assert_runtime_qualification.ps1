[CmdletBinding()]
param([string]$Root, [string]$RequiredBinaryPath)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'runtime_qualification.ps1')
$null=Assert-VpRuntimeQualification $Root $RequiredBinaryPath
Write-Host 'Mandatory runtime qualification matches the exact source, binaries and both executed test variants.'
