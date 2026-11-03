[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BinaryPath,
    [Parameter(Mandatory = $true)][version]$ToolsetVersion
)
$ErrorActionPreference = 'Stop'
# Resolve the running host's modules, not a different edition earlier in PSModulePath.
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Utility\Microsoft.PowerShell.Utility.psd1') -Force -ErrorAction Stop
Import-Module (Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Management\Microsoft.PowerShell.Management.psd1') -Force -ErrorAction Stop
# Build-only sidecar, deliberately excluded from the release package.
[ordered]@{
    schemaVersion = 1
    configuration = 'Release'
    architecture = 'x64'
    toolsetVersion = $ToolsetVersion.ToString()
    sha256 = (Get-FileHash -LiteralPath $BinaryPath -Algorithm SHA256).Hash
} | ConvertTo-Json | Set-Content -LiteralPath ($BinaryPath + '.runtime.json') -Encoding UTF8
