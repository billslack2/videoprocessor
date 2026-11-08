# Copyright(C) 2026 VP-V contributors
# SPDX-License-Identifier: GPL-3.0-only
#
# Builds and runs the pattern library's stand-alone tests with cl.exe only:
# no solution, no MSBuild project, no pch.h, no renderer. Needs a Visual Studio
# C++ toolset (any recent version); it is found with vswhere.
#
#   powershell -File run_pattern_lib_tests.ps1 [-OutDir <scratch folder>]
#
# Exit code 0 = built and every check passed.
param([string]$OutDir = (Join-Path $env:TEMP 'vp-pattern-lib-tests'))
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found: install Visual Studio with the C++ workload.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio with the C++ x64 tools found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'

New-Item -ItemType Directory -Force $OutDir | Out-Null
$exe = Join-Path $OutDir 'PatternLibSelfTest.exe'
$sources = @('ColorMath.cpp', 'PatternCodes.cpp', 'PatternPlan.cpp', 'tests\PatternLibSelfTest.cpp') | ForEach-Object { '"' + (Join-Path $here $_) + '"' }
$cl = 'cl.exe /nologo /std:c++17 /EHsc /W4 /WX /permissive- /Fo"' + $OutDir + '\\" /Fe"' + $exe + '" ' + ($sources -join ' ')
cmd /c "`"$vcvars`" >nul && $cl"
if ($LASTEXITCODE -ne 0) { throw "compile failed ($LASTEXITCODE)" }
& $exe
exit $LASTEXITCODE
