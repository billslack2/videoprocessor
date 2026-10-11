[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'build_updater.ps1')
$output=Join-Path $root 'artifacts\updater'
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:exe /platform:x64 /optimize+ "/out:$output\UpdateControlTests.exe" "/r:$output\VideoProcessorUpdate.exe" /r:System.Web.Extensions.dll (Join-Path $root 'src\VideoProcessor-Update\UpdateControlTests.cs')
if($LASTEXITCODE -ne 0){throw 'Update control test compilation failed'}
& "$output\UpdateControlTests.exe" "$output\VideoProcessorUpdate.exe"
if($LASTEXITCODE -ne 0){throw 'Update control tests failed'}
