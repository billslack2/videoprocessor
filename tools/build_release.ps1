[CmdletBinding()]
param(
    [ValidateSet('all','runtime','config')][string]$Phase='all',
    [string]$MagewellSdkRoot='E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3',
    [string]$MSBuildPath,
    [switch]$IncludeTests,
    [switch]$Rebuild,
    [switch]$PlanOnly
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$vpRoot=Split-Path -Parent $PSScriptRoot
$vpVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
function Find-BuildHost([string]$range) {
    if(!(Test-Path -LiteralPath $vpVswhere)){throw 'Install vswhere or specify explicit MSBuild paths.'}
    $install=& $vpVswhere -latest -products '*' -version $range -requires Microsoft.Component.MSBuild -property installationPath
    if($LASTEXITCODE -ne 0 -or !$install){throw "No Visual Studio installation in version range $range"}
    Join-Path $install 'MSBuild\Current\Bin\MSBuild.exe'
}
$targets=@()
if($Phase -ne 'config') {
    $targets+=@('VideoProcessor-GUI','VideoProcessor-VPRenderer','VideoProcessor-VPRendererBeta','VideoProcessor-ConfigDiscovery')
    if($IncludeTests){$targets+='VideoProcessor-Test'}
}
if($Phase -ne 'runtime') {
    $targets+=@('VideoProcessor-Config','VideoProcessor-OutputProbe')
    if($IncludeTests){$targets+='VideoProcessor-ConfigTests'}
}
if(!$MSBuildPath){$MSBuildPath=Find-BuildHost '[17.0,19.0)'}
$vpPhases=@(@{name=$Phase;engine=$MSBuildPath;major=0;targets=$targets})
foreach($vpPhase in $vpPhases) {
    $vpPhase.engine=(Resolve-Path -LiteralPath $vpPhase.engine).Path
    $version=(Get-Item -LiteralPath $vpPhase.engine).VersionInfo.FileMajorPart
    if(($vpPhase.major -and $version -ne $vpPhase.major) -or (!$vpPhase.major -and $version -lt 17)) {
        throw "Wrong MSBuild host for $($vpPhase.name): $($vpPhase.engine)"
    }
    Write-Host "$($vpPhase.name): $($vpPhase.engine); targets=$($vpPhase.targets -join ',')"
}
if($PlanOnly){return}
$vpVsRoot=Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MSBuildPath)))
$vpTools=Join-Path $vpVsRoot 'VC\Tools\MSVC\14.44.35207'
foreach($relative in @('bin\Hostx64\x64\cl.exe','bin\Hostx64\x64\link.exe','atlmfc\include\afxwin.h','atlmfc\lib\x64\mfc140u.lib')) {
    if(!(Test-Path -LiteralPath (Join-Path $vpTools $relative))){throw "MSVC 14.44.35207 with x64 MFC/ATL required: missing $relative under $vpTools"}
}
if($Phase -ne 'config' -and !(Test-Path -LiteralPath (Join-Path $MagewellSdkRoot 'Include\LibMWCapture\MWCapture.h'))) {
    throw "Magewell SDK header missing under $MagewellSdkRoot"
}
$vpOutput=Join-Path $vpRoot 'artifacts\release-build'
$null=New-Item -ItemType Directory -Force -Path $vpOutput
# Serialize this entry point. Never launch two builds against the same intermediates.
$vpLock=[IO.File]::Open((Join-Path $vpOutput 'build.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
$vpSaved=@{}
# Every project uses the repository-pinned v143 paths. Developer-shell overrides must
# not inject another compiler's headers, CRT libraries, flags or MSBuild targets.
$vpEnvironment=@('CL','_CL_','LINK','_LINK_','LIB','LIBPATH','INCLUDE','VCTargetsPath',
    'VCToolsVersion','VCToolsInstallDir','VCINSTALLDIR','VSINSTALLDIR','VisualStudioVersion')
Push-Location -LiteralPath $vpRoot
try {
    foreach($name in $vpEnvironment){$vpSaved[$name]=[Environment]::GetEnvironmentVariable($name,'Process');[Environment]::SetEnvironmentVariable($name,$null,'Process')}
    foreach($vpPhase in $vpPhases) {
        $targetList=($vpPhase.targets | ForEach-Object {if($Rebuild){"${_}:Rebuild"}else{$_}}) -join ';'
        $stamp=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
        $log=Join-Path $vpOutput "$stamp-$($vpPhase.name).log"
        $arguments=@('VideoProcessor.sln',"/t:$targetList",'/m','/nr:false',
            '/p:Configuration=Release','/p:Platform=x64','/p:PreferredToolArchitecture=x64',
            '/p:UseEnv=false',"/p:MagewellSdkRoot=$MagewellSdkRoot",'/v:minimal','/nologo',
            '/fl',"/flp:logfile=$log;verbosity=normal")
        & $vpPhase.engine @arguments
        $buildExit=$LASTEXITCODE
        [ordered]@{phase=$vpPhase.name;msbuild=$vpPhase.engine;
            msbuildVersion=(Get-Item -LiteralPath $vpPhase.engine).VersionInfo.FileVersion;
            platformToolset='v143';vcToolsVersion='14.44.35207';
            compiler=(Join-Path $vpTools 'bin\Hostx64\x64\cl.exe');
            linker=(Join-Path $vpTools 'bin\Hostx64\x64\link.exe');
            compilerVersion=(Get-Item -LiteralPath (Join-Path $vpTools 'bin\Hostx64\x64\cl.exe')).VersionInfo.FileVersion;
            linkerVersion=(Get-Item -LiteralPath (Join-Path $vpTools 'bin\Hostx64\x64\link.exe')).VersionInfo.FileVersion;
            arguments=$arguments;exitCode=$buildExit;log=$log} |
            ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $vpOutput "$stamp-$($vpPhase.name).json") -Encoding UTF8
        if($buildExit -ne 0){throw "$($vpPhase.name) Release build failed ($buildExit). Inspect $log; no automatic rebuild/retry was attempted."}
    }
} finally {
    foreach($name in $vpSaved.Keys){[Environment]::SetEnvironmentVariable($name,$vpSaved[$name],'Process')}
    Pop-Location
    $vpLock.Dispose()
}
