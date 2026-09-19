[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$BinaryPath,
    [Parameter(Mandatory = $true)][version]$ToolsetVersion
)
$ErrorActionPreference = 'Stop'
Import-Module Microsoft.PowerShell.Utility
# Build-only sidecar, deliberately excluded from the release package.
[ordered]@{
    schemaVersion = 1
    configuration = 'Release'
    architecture = 'x64'
    toolsetVersion = $ToolsetVersion.ToString()
    sha256 = (Get-FileHash -LiteralPath $BinaryPath -Algorithm SHA256).Hash
} | ConvertTo-Json | Set-Content -LiteralPath ($BinaryPath + '.runtime.json') -Encoding UTF8
