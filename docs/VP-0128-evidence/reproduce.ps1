[CmdletBinding()]
param([string]$VsInstallPath = 'C:\Program Files\Microsoft Visual Studio\18\Professional')
$ErrorActionPreference = 'Stop'
$vpAuditRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vpAuditOutput = Join-Path $vpAuditRoot 'artifacts\vp0128-reproduce'
$vpAuditProject = Join-Path $vpAuditRoot 'src\VideoProcessor-ConfigTests\VP0128AuditRepro.vcxproj'
if (Test-Path -LiteralPath $vpAuditProject) { throw 'Existing audit project: preserve it or select a clean worktree.' }
New-Item -ItemType Directory -Path $vpAuditOutput -Force | Out-Null
$project = [IO.File]::ReadAllText((Join-Path $vpAuditRoot 'src\VideoProcessor-ConfigTests\VideoProcessor-ConfigTests.vcxproj'))
$project = $project.Replace('<ClCompile Include="ConfigEditorWindowTests.cpp" />', '<ClCompile Include="..\..\docs\VP-0128-evidence\ConfigProbe.cpp" />').Replace('<TargetName>VideoProcessorConfigTests</TargetName>', '<TargetName>VP0128ConfigProbe</TargetName>').Replace('<IntDir>$(ProjectDir)x64\$(Configuration)\</IntDir>', '<IntDir>$(ProjectDir)x64\VP0128AuditRepro\</IntDir>')
[IO.File]::WriteAllText($vpAuditProject, $project)
$previousPlatform = $env:QT_QPA_PLATFORM
Push-Location $vpAuditRoot
try {
    & (Join-Path $VsInstallPath 'MSBuild\Current\Bin\MSBuild.exe') $vpAuditProject /m /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$vpAuditRoot\" /v:minimal *> (Join-Path $vpAuditOutput 'config-build.log')
    if ($LASTEXITCODE -ne 0) { throw 'Config probe build failed; see log.' }
    $env:QT_QPA_PLATFORM = 'offscreen'
    & (Join-Path $vpAuditRoot 'x64\Release\VP0128ConfigProbe.exe') *> (Join-Path $vpAuditOutput 'config-probe.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Config probe failed.' }
    & (Join-Path $VsInstallPath 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
    & cl.exe /nologo /EHsc /MD /std:c++17 /I3rdparty/libplacebo/include docs/VP-0128-evidence/NativeProbe.cpp src/VideoProcessor-Lib/vprenderer/LibplaceboRenderParameters.cpp "/Fo$vpAuditOutput\" /Fex64/Release/VP0128NativeProbe.exe /link 3rdparty/libplacebo/lib/libplacebo-360.lib *> (Join-Path $vpAuditOutput 'native-build.log')
    if ($LASTEXITCODE -ne 0) { throw 'Native probe build failed.' }
    # The ordinary Release solution/test build supplies libplacebo dependencies beside the EXE.
    & (Join-Path $vpAuditRoot 'x64\Release\VP0128NativeProbe.exe') > (Join-Path $vpAuditOutput 'native-probe.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Native probe failed; first build the ordinary Release solution for DLL staging.' }
    Write-Output "Audit observations written to $vpAuditOutput. These are characterization probes, not assertions that known defects are fixed."
} finally {
    $env:QT_QPA_PLATFORM = $previousPlatform
    Pop-Location
    Remove-Item -LiteralPath $vpAuditProject
}