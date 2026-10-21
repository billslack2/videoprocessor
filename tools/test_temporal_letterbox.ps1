# Compile and run the real D3D11 letterbox regression against both bundled APIs.
[CmdletBinding()]
param(
    [string]$VcVarsPath='C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvars64.bat',
    [string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
if (!$OutputDirectory) {$OutputDirectory=Join-Path $repo 'artifacts\temporal-letterbox'}
$output=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null
if (!(Test-Path -LiteralPath $VcVarsPath)) {throw "Missing MSVC x64 environment: $VcVarsPath"}
foreach ($variant in @(@{suffix='';api=360},@{suffix='-beta';api=377})) {
    $runtime=Join-Path $repo ('3rdparty\libplacebo'+$variant.suffix)
    $name='temporal-letterbox-'+$variant.api
    $exe=Join-Path $output ($name+'.exe')
    $cmd=Join-Path $output ($name+'.cmd')
    @"
@echo off
call "$VcVarsPath" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /MD /EHsc /O2 /std:c++17 /I "$repo\src\VideoProcessor-Lib" /I "$runtime\include" "$PSScriptRoot\test_temporal_letterbox.cpp" /Fo"$output\$name.obj" /Fe"$exe" /link "$runtime\lib\libplacebo-$($variant.api).lib"
if errorlevel 1 exit /b %errorlevel%
set "PATH=$runtime\bin;%PATH%"
"$exe"
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd
    if ($LASTEXITCODE) {throw "Temporal letterbox regression failed for API $($variant.api) (exit $LASTEXITCODE)"}
}
