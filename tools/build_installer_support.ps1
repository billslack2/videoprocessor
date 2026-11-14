[CmdletBinding()]
param(
    [string]$OutputPath,
    [switch]$FixtureIdentity
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root 'src\VideoProcessor-InstallSupport'
if (-not $OutputPath) {
    $OutputPath = Join-Path $root 'artifacts\installer-support\VideoProcessorInstallSupport.exe'
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$null = New-Item -ItemType Directory -Path (Split-Path -Parent $OutputPath) -Force
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $csc)) { throw 'The x64 .NET Framework C# compiler is required.' }
$framework = Split-Path -Parent $csc
$sources = @('Program.cs','ManifestAndPaths.cs','Plan.cs','Transactions.cs' | ForEach-Object {
    Join-Path $project $_
})
$compilerArgs = @(
    '/nologo','/target:exe','/platform:x64','/optimize+','/debug-','/warn:4','/utf8output',
    "/out:$OutputPath",
    "/reference:$(Join-Path $framework 'System.dll')",
    "/reference:$(Join-Path $framework 'System.Core.dll')",
    "/reference:$(Join-Path $framework 'System.Web.Extensions.dll')"
)
if ($FixtureIdentity) { $compilerArgs += '/define:INSTALL_SUPPORT_TEST_IDENTITY' }
$compilerArgs += $sources
& $csc @compilerArgs
if ($LASTEXITCODE -ne 0) { throw "Installer helper compilation failed with exit code $LASTEXITCODE." }
Write-Host "x64 Release installer helper: $OutputPath"

