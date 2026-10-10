$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$output=Join-Path $root 'artifacts\updater-tests'
$null=New-Item -ItemType Directory -Force $output
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:exe /platform:x64 /optimize+ "/out:$output\UpdateTests.exe" /r:System.Web.Extensions.dll (Join-Path $root 'src\VideoProcessor-Update\UpdateCore.cs') (Join-Path $root 'src\VideoProcessor-Update\UpdateTests.cs')
if ($LASTEXITCODE -ne 0) {throw 'Updater test build failed.'}
& "$output\UpdateTests.exe"
if ($LASTEXITCODE -ne 0) {throw 'Updater policy checks failed.'}
