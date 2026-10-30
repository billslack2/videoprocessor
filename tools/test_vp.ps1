<#
.SYNOPSIS
Build and run x64 Release native Smoke, Targeted, or Full tests with preserved evidence.
.DESCRIPTION
Smoke is a development gate, not release approval. See docs/TESTING.md for the
additional Config, beta renderer, package and live-playback requirements.
#>
[CmdletBinding()]
param(
    [ValidateSet('Smoke','Targeted','Full')][string]$Suite='Smoke',
    [string[]]$Classes=@(),
    [ValidateSet('standard','beta')][string]$Variant='standard',
    [string]$VsInstallPath,
    [string]$MagewellSdkRoot='E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3',
    [string]$OutputDirectory,
    [switch]$List
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
. (Join-Path $PSScriptRoot 'test_suite_support.ps1')
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'test-smoke.json') -Raw | ConvertFrom-Json
$selectedClasses=@(); $selectedMethods=@(); $excludedMethods=@()
if ($Suite -ne 'Targeted' -and $Classes.Count) { throw '-Classes requires -Suite Targeted.' }
if ($Suite -eq 'Smoke') {
    $selectedClasses=@($manifest.groups | ForEach-Object { $_.classes })
    $selectedMethods=@($manifest.methods); $excludedMethods=@($manifest.excludedMethods)
} elseif ($Suite -eq 'Targeted') {
    if (!$Classes.Count) { throw 'Targeted requires at least one exact native class name.' }
    $selectedClasses=@($Classes | Select-Object -Unique)
}
foreach ($class in @($selectedClasses)+@($selectedMethods | ForEach-Object { $_.class })) {
    if ($class -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') { throw "Invalid class: $class" }
}
$filters=@($selectedClasses | ForEach-Object { "FullyQualifiedName~${_}::" })
$filters+=@($selectedMethods | ForEach-Object {
    if ($_.method -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') { throw 'Invalid smoke method.' }
    "FullyQualifiedName~$($_.class)::$($_.method)"
})
$filter=$filters -join '|'
if ($excludedMethods.Count) {
    $exclusions=@($excludedMethods | ForEach-Object {
        if ($_.class -notmatch '^[A-Za-z_][A-Za-z0-9_]*$' -or $_.method -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') { throw 'Invalid excluded diagnostic.' }
        "FullyQualifiedName!~$($_.class)::$($_.method)"
    })
    $filter='('+$filter+')&'+($exclusions -join '&')
}
if ($List) {
    [pscustomobject]@{suite=$Suite;variant=$Variant;classes=$selectedClasses;methods=$selectedMethods;excludedMethods=$excludedMethods;filter=$filter}
    return
}
if (!$VsInstallPath) {
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $VsInstallPath=(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath) -join ''
    if ($LASTEXITCODE -ne 0 -or !$VsInstallPath) { throw 'Pass -VsInstallPath for an installed Visual Studio with C++ tests.' }
}
$msbuild=Join-Path $VsInstallPath 'MSBuild\Current\Bin\MSBuild.exe'
$vstest=Join-Path $VsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe'
foreach ($required in @($msbuild,$vstest,(Join-Path $MagewellSdkRoot 'Include\LibMWCapture\MWCapture.h'))) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing prerequisite: $required" }
}
if (!$OutputDirectory) { $OutputDirectory=Join-Path $root ('artifacts\tests-'+$Suite.ToLower()+'-'+[guid]::NewGuid().ToString('N')) }
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Choose a new results directory; previous evidence is never overwritten.' }
New-Item -ItemType Directory -Path $output | Out-Null
. (Join-Path $PSScriptRoot 'installer_build_identity.ps1')
$identity=Get-VpSourceIdentity $root
$receipt=[ordered]@{schemaVersion=1;suite=$Suite;variant=$Variant;source=$identity;configuration='Release';architecture='x64';
    selection=@{classes=$selectedClasses;methods=$selectedMethods;excludedMethods=$excludedMethods;filter=$filter};
    msbuild=$msbuild;vstest=$vstest;magewellSdkRoot=$MagewellSdkRoot;buildSeconds=$null;testSeconds=$null;
    binary=$null;binarySha256=$null;dependencySha256=$null;runtimeArtifacts=@();passed=0;success=$false;error=$null;
    scope='Native tests only. See docs/TESTING.md for release and deployment gates.'}
try {
    $watch=[Diagnostics.Stopwatch]::StartNew()
    $projects=@('src\VideoProcessor-VPRenderer\VideoProcessor-VPRenderer.vcxproj',
        'src\VideoProcessor-VPRenderer\VideoProcessor-VPRendererBeta.vcxproj',
        'src\VideoProcessor-Test\VideoProcessor-Test.vcxproj.vcxproj')
    Write-Host "Building renderer prerequisites and $Variant x64 Release native tests; evidence: $output"
    foreach ($project in $projects) {
        $buildArgs=@((Join-Path $root $project),'/t:Build','/m:4','/p:Configuration=Release','/p:Platform=x64',
            "/p:SolutionDir=$root\","/p:MagewellSdkRoot=$MagewellSdkRoot","/p:VPTestLibplacebo=$Variant",'/v:minimal','/nologo')
        & $msbuild @buildArgs *>> (Join-Path $output 'build.log')
        $receipt.buildSeconds=$watch.Elapsed.TotalSeconds
        if ($LASTEXITCODE -ne 0) { throw "Build failed for $project ($LASTEXITCODE). See build.log." }
    }
    foreach ($relative in @('vprenderer\VideoProcessorVPRenderer.dll','vprenderer\libplacebo-360.dll',
        'vprenderer-beta\VideoProcessorVPRendererBeta.dll','vprenderer-beta\libplacebo-388.dll')) {
        $path=Join-Path $root ('x64\Release\'+$relative)
        $receipt.runtimeArtifacts+=@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash}
    }
    $bin=Join-Path $root 'x64\Release'
    $dll='VideoProcessor-Test.dll'; $dependency='libplacebo-360.dll'; $dependencyFolder='libplacebo'
    if ($Variant -eq 'beta') {
        $bin=Join-Path $bin 'tests-beta'; $dll='VideoProcessor-TestBeta.dll'; $dependency='libplacebo-388.dll'; $dependencyFolder='libplacebo-beta'
    }
    $receipt.binary=Join-Path $bin $dll
    $receipt.binarySha256=(Get-FileHash -LiteralPath $receipt.binary).Hash
    $receipt.dependencySha256=(Get-FileHash -LiteralPath (Join-Path $bin $dependency)).Hash
    if ($receipt.dependencySha256 -ne (Get-FileHash -LiteralPath (Join-Path $root "3rdparty\$dependencyFolder\bin\$dependency")).Hash) {
        throw 'Test dependency differs from the selected bundled renderer.'
    }
    $testArgs=@($receipt.binary,'/Platform:x64',"/ResultsDirectory:$output",'/Logger:trx;LogFileName=tests.trx')
    if ($filter) { $testArgs+="/TestCaseFilter:$filter" }
    if ($Suite -eq 'Smoke') {
        $timeout=[int]$manifest.timeoutSeconds*1000
        "<RunSettings><RunConfiguration><TestSessionTimeout>$timeout</TestSessionTimeout></RunConfiguration></RunSettings>" |
            Set-Content -LiteralPath (Join-Path $output 'smoke.runsettings') -Encoding UTF8
        $testArgs+='/Settings:'+(Join-Path $output 'smoke.runsettings')
    }
    Write-Host "Running $Suite ($Variant)..."
    $watch.Restart()
    Push-Location -LiteralPath $bin
    try {
        # Windows PowerShell may turn native stderr into errors before LASTEXITCODE can be inspected.
        $ErrorActionPreference='Continue'
        & $vstest @testArgs *> (Join-Path $output 'tests.log')
        $testExit=$LASTEXITCODE
    } finally { $ErrorActionPreference='Stop'; Pop-Location; $receipt.testSeconds=$watch.Elapsed.TotalSeconds }
    if ($testExit -ne 0) { throw "Native tests failed ($testExit). See tests.log and tests.trx." }
    $results=@(Read-VpTestResults (Join-Path $output 'tests.trx'))
    if ($Suite -ne 'Full') { Assert-VpTestSelection $results $selectedClasses $selectedMethods $excludedMethods }
    if ($Suite -eq 'Smoke' -and $receipt.testSeconds -gt [int]$manifest.timeoutSeconds) { throw 'Smoke exceeded its time budget.' }
    $after=Get-VpSourceIdentity $root
    if ($after.fingerprint -ne $identity.fingerprint) { throw 'Sources changed during the run; rerun on the final source.' }
    if ((Get-FileHash -LiteralPath $receipt.binary).Hash -ne $receipt.binarySha256 -or
        (Get-FileHash -LiteralPath (Join-Path $bin $dependency)).Hash -ne $receipt.dependencySha256) { throw 'Test binaries changed during the run.' }
    foreach ($artifact in $receipt.runtimeArtifacts) {
        if ((Get-FileHash -LiteralPath $artifact.path).Hash -ne $artifact.sha256) { throw 'Renderer artifacts changed during the run.' }
    }
    $results | Sort-Object seconds -Descending | ConvertTo-Json -Depth 3 |
        Set-Content -LiteralPath (Join-Path $output 'timings.json') -Encoding UTF8
    $receipt.passed=$results.Count; $receipt.success=$true
    Write-Host ("PASS: {0} tests; build {1:N1}s; tests {2:N1}s. {3}" -f $results.Count,$receipt.buildSeconds,$receipt.testSeconds,$output)
} catch {
    $receipt.error=$_.Exception.Message
    throw
} finally {
    $receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'test-receipt.json') -Encoding UTF8
}
