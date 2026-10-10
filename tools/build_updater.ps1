[CmdletBinding()]
param([string]$OutputPath, [string]$PublicKeyPath)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) { $OutputPath=Join-Path $root 'artifacts\updater\VideoProcessorUpdate.exe' }
if (-not $PublicKeyPath) { $PublicKeyPath=Join-Path $root 'packaging\updates\UpdatePublicKey.xml' }
if (-not (Test-Path -LiteralPath $PublicKeyPath)) { throw 'Initialize the update signing key first: tools/new_update_signing_key.ps1.' }
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$null=New-Item -ItemType Directory -Force (Split-Path -Parent $OutputPath)
& $compiler /nologo /target:winexe /platform:x64 /optimize+ "/out:$OutputPath" /r:System.Web.Extensions.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Management.dll "/resource:$PublicKeyPath,UpdatePublicKey.xml" (Join-Path $root 'src\VideoProcessor-Update\UpdateCore.cs') (Join-Path $root 'src\VideoProcessor-Update\Program.cs') (Join-Path $root 'src\VideoProcessor-Update\UpdateControl.cs')
if ($LASTEXITCODE -ne 0) { throw 'Updater compilation failed.' }
Write-Host "Updater: $OutputPath"
