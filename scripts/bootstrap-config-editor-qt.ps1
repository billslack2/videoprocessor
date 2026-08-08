param(
    [string]$QtVersion = "6.8.3",
    [string]$Architecture = "win64_msvc2022_64"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $repositoryRoot ".dependencies"
$aqtRoot = Join-Path $dependencyRoot "aqtinstall"
$qtRoot = Join-Path $dependencyRoot "Qt"
$qtKit = Join-Path $qtRoot "$QtVersion\msvc2022_64"

if (Test-Path -LiteralPath (Join-Path $qtKit "bin\qmake.exe")) {
    Write-Host "Qt $QtVersion is already available at $qtKit"
    exit 0
}

New-Item -ItemType Directory -Force -Path $aqtRoot | Out-Null
python -m pip install --disable-pip-version-check --target $aqtRoot "aqtinstall==3.3.0"
if ($LASTEXITCODE -ne 0) { throw "Could not install the Qt bootstrap tool." }

$env:PYTHONPATH = $aqtRoot
python -m aqt install-qt windows desktop $QtVersion $Architecture -O $qtRoot
if ($LASTEXITCODE -ne 0) { throw "Could not install Qt $QtVersion." }

Write-Host "Qt $QtVersion installed at $qtKit"
