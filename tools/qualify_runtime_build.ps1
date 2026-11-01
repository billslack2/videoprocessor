[CmdletBinding()]
param(
    [string]$Root,
    [string]$MagewellSdkRoot,
    [string]$MSBuildPath,
    [string]$RequiredBinaryPath,
    [string]$Configuration='Release',
    [string]$Platform='x64',
    [ValidateSet('Qualification','Native','Focused')][string]$Lane='Qualification',
    [string[]]$Methods=@(),
    [ValidateRange(1,4)][int]$BuildParallelism=2,
    [switch]$ColdShaderCache
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'runtime_qualification.ps1')
Assert-VpQualificationTestEnvironment
$Root=Get-VpQualificationRoot $Root
Assert-VpQualificationConfiguration $Configuration $Platform
if ($Lane -ne 'Native') { & (Join-Path $Root 'tools\test_d3d11_submit_contract.ps1') -RepositoryRoot $Root }
. (Join-Path $Root 'tools\installer_build_identity.ps1')
if (-not $MSBuildPath) {
    $where=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $where) {
        $MSBuildPath=& $where -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
    }
}
if (-not $MSBuildPath -or -not (Test-Path -LiteralPath $MSBuildPath -PathType Leaf)) { throw 'Pass -MSBuildPath with the installed Visual Studio MSBuild.exe.' }
$MSBuildPath=[IO.Path]::GetFullPath($MSBuildPath)
$vsDirectory=Split-Path -Parent $MSBuildPath;$vstest=''
for ($level=0;$level -lt 6 -and $vsDirectory;$level++) {
    $candidate=Join-Path $vsDirectory 'Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $vstest=$candidate;break }
    $vsDirectory=Split-Path -Parent $vsDirectory
}
if (-not $vstest) { throw 'The selected Visual Studio installation must include native C++ test tools.' }
if (-not $MagewellSdkRoot) { $MagewellSdkRoot=$env:MagewellSdkRoot }
if (-not $MagewellSdkRoot) { $MagewellSdkRoot=Join-Path $Root '3rdparty\magewell\SDKv3' }
if (-not (Test-Path -LiteralPath (Join-Path $MagewellSdkRoot 'Include\LibMWCapture\MWCapture.h'))) { throw 'Pass -MagewellSdkRoot with the official capture SDK used by the runtime build.' }
$MagewellSdkRoot=[IO.Path]::GetFullPath($MagewellSdkRoot)
$plan=Get-VpCriticalTestPlan $Root
. (Join-Path $Root 'tools/runtime_iteration.ps1')
$plan=Get-VpIterationTestPlan $plan $Lane $Methods

$sha=[Security.Cryptography.SHA256]::Create()
try { $lockId=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Root.ToLowerInvariant())))).Replace('-','') }
finally { $sha.Dispose() }
$mutex=[Threading.Mutex]::new($false,('Local\VP-runtime-qualification-'+$lockId))
$totalTimer=[Diagnostics.Stopwatch]::StartNew()
$locked=$false;$runs=@();$builds=@();$source=$null;$binaries=@();$runDirectory='';$success=$false
$oldNonce=$env:VP_CRITICAL_GATE_NONCE;$oldRoot=$env:VP_CRITICAL_GATE_ROOT
$oldCacheDirectory=$env:VP_SHADER_CACHE_DIRECTORY
if (-not $env:VP_SHADER_CACHE_DIRECTORY) { $env:VP_SHADER_CACHE_DIRECTORY=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'VideoProcessor\TestShaderCache' }
$receiptPath=Get-VpQualificationReceiptPath $Root
try {
    try { $locked=$mutex.WaitOne([TimeSpan]::FromMinutes(30)) } catch [Threading.AbandonedMutexException] { $locked=$true }
    if (-not $locked) { throw 'Timed out waiting for another qualification in this worktree; runtime remains unqualified.' }
    $null=New-Item -ItemType Directory -Force -Path (Split-Path -Parent $receiptPath)
    # A failed/new qualification must not leave a previously green stamp usable.
    if (Test-Path -LiteralPath $receiptPath) { Remove-Item -LiteralPath $receiptPath -Force }
    $source=Get-VpSourceIdentity $Root
    $runDirectory=Join-Path $Root ('artifacts\qualification\runs\'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
    $null=New-Item -ItemType Directory -Force -Path $runDirectory
    $coldCache=($Lane -ne 'Native' -and ($Lane -eq 'Qualification' -or $ColdShaderCache))
    if($coldCache -and $Lane -ne 'Native') {
        # Every qualification starts from an empty application shader cache.
        # Later cases reuse it; cold warmup is exercised once per dependency.
        $env:VP_SHADER_CACHE_DIRECTORY=Join-Path $runDirectory 'shader-cache'
        $null=New-Item -ItemType Directory -Path $env:VP_SHADER_CACHE_DIRECTORY
    }
    $nonce=[Guid]::NewGuid().ToString('N');$env:VP_CRITICAL_GATE_NONCE=$nonce;$env:VP_CRITICAL_GATE_ROOT=$Root
    $common=@('/t:Build','/m:1','/nr:false','/p:Configuration=Release','/p:Platform=x64',
        "/p:SolutionDir=$Root\", "/p:MagewellSdkRoot=$MagewellSdkRoot",'/p:VPCriticalGateChild=true',
        "/p:VPCriticalGateNonce=$nonce",'/p:BuildingSolutionFile=false','/p:BuildingInsideVisualStudio=false',
        '/p:ForceImportBeforeCppTargets=','/p:ForceImportAfterCppTargets=','/v:minimal','/nologo')
    $projects=@(
        # Dependencies of this library contain no runtime projects. Parent runtime
        # references already finished; serial child processes never wait on the
        # parent's MSBuild scheduler or re-enter its qualification hook.
        @{name='native-library';path='src\VideoProcessor-Lib\VideoProcessor-Lib.vcxproj';extra=@('/p:BuildProjectReferences=true',"/p:OutDir=$Root\x64\Release\")},
        @{name='host';path='src\VideoProcessor-GUI\VideoProcessor-GUI.vcxproj';extra=@('/p:BuildProjectReferences=false',"/p:OutDir=$Root\x64\Release\")},
        @{name='standard-renderer';path='src\VideoProcessor-VPRenderer\VideoProcessor-VPRenderer.vcxproj';extra=@('/p:BuildProjectReferences=false')},
        @{name='beta-renderer';path='src\VideoProcessor-VPRenderer\VideoProcessor-VPRendererBeta.vcxproj';extra=@('/p:BuildProjectReferences=false')},
        @{name='standard-tests';path='src\VideoProcessor-Test\VideoProcessor-Test.vcxproj.vcxproj';extra=@('/p:BuildProjectReferences=false','/p:VPTestLibplacebo=standard',"/p:OutDir=$Root\x64\Release\")},
        @{name='beta-tests';path='src\VideoProcessor-Test\VideoProcessor-Test.vcxproj.vcxproj';extra=@('/p:BuildProjectReferences=false','/p:VPTestLibplacebo=beta')}
    )
    if ($Lane -eq 'Native') {
        $nativeSources=New-VpCriticalNativeSourceList $Root (Join-Path $runDirectory 'critical-native-sources.props')
        $projects=@($projects | Where-Object {$_.name -eq 'native-library'})+@(
            @{name='critical-native-tests';path='src\VideoProcessor-Test\VideoProcessor-CriticalNativeTests.vcxproj';
              extra=@('/p:BuildProjectReferences=false','/p:VPTestLibplacebo=standard',"/p:VPCriticalNativeSourcesFile=$($nativeSources.path)")})
    }
    elseif ($Lane -eq 'Focused') { $projects=@($projects | Where-Object {$_.name -ne 'beta-tests'}) }
    # Renderers share version.h. Generate it once before parallel compilation;
    # all object, PDB and output paths of the companion projects are distinct.
    if ($Lane -ne 'Native') {
        & (Join-Path $Root 'tools/generate_version.ps1') -Project (Join-Path $Root 'src/VideoProcessor-VPRenderer') -GitRoot $Root *> (Join-Path $runDirectory 'renderer-version.log')
        foreach($project in $projects) { if($project.name -like '*-renderer') { $project.extra+= '/p:PreBuildEventUseInBuild=false' } }
    }
    . (Join-Path $Root 'tools/runtime_build_jobs.ps1')
    # Complete shared library dependencies before launching independent consumers.
    $builds+=@(Invoke-VpBuildBatch $MSBuildPath @($projects[0]) $common $Root $runDirectory 1)
    $builds+=@(Invoke-VpBuildBatch $MSBuildPath @($projects | Select-Object -Skip 1) $common $Root $runDirectory $BuildParallelism)
    if ($Lane -eq 'Qualification') { $binaries=Get-VpQualifiedOutputInventory $Root } else { $binaries=Get-VpIterationOutputInventory $Root $Lane }
    $variants=if($Lane -eq 'Qualification'){@('standard','beta')}else{@('standard')}
    foreach ($variant in $variants) {
        $runPlan=if($Lane -eq 'Qualification'){Get-VpCriticalVariantPlan $plan $variant}else{$plan}
        $binary=if($Lane -eq 'Native'){'x64\Release\tests-critical-native\VideoProcessor-CriticalNativeTests.dll'}elseif($variant -eq 'standard'){'x64\Release\VideoProcessor-Test.dll'}else{'x64\Release\tests-beta\VideoProcessor-TestBeta.dll'}
        $run=[ordered]@{variant=$variant;buildExit=0;testExit=$null;binary=$binary;trx=$null;trxSha256=$null;executed=0;native=0;actualClassic=0}
        $runs+=,$run
        $settings=Join-Path $runDirectory 'critical.runsettings'
        # The six persistent advanced methods each exercise two input rates.
        # Measured cold shader preparation takes 122 seconds per renderer, so
        # those cases alone can exceed the former 20-minute session cap. Each
        # case retains its own bounded startup and steady-state assertions.
        '<RunSettings><RunConfiguration><TestSessionTimeout>3600000</TestSessionTimeout></RunConfiguration></RunSettings>' | Set-Content -LiteralPath $settings -Encoding UTF8
        Write-Host "$Lane lane: execute $($runPlan.nativeCount) native contracts and $($runPlan.actualCount) real AR/subtitle presentation tests [$variant]"
        Push-Location -LiteralPath (Split-Path -Parent (Join-Path $Root $binary))
        try {
            $testArguments=@((Join-Path $Root $binary),'/Platform:x64',"/Settings:$settings","/TestCaseFilter:$($runPlan.filter)","/Logger:trx;LogFileName=$variant.trx","/ResultsDirectory:$runDirectory")
            $testTimer=[Diagnostics.Stopwatch]::StartNew()
            $run.testExit=Invoke-VpQualificationNativeProcess $vstest $testArguments (Join-Path $runDirectory ($variant+'-tests.log'))
            $run.seconds=[Math]::Round($testTimer.Elapsed.TotalSeconds,3)
        } finally { Pop-Location }
        $trx=Join-Path $runDirectory ($variant+'.trx')
        if(Test-Path -LiteralPath $trx -PathType Leaf) {
            $run.trx=$trx.Substring($Root.Length+1).Replace('\','/');$run.trxSha256=(Get-FileHash -LiteralPath $trx -Algorithm SHA256).Hash
        }
        if ($run.testExit -ne 0) { throw "Mandatory $variant native/actual-renderer contracts failed. See $runDirectory" }
        $counts=Read-VpCriticalTrx $trx $runPlan
        $run.executed=$counts.executed;$run.native=$counts.native;$run.actualClassic=$counts.actualClassic
        $expectedDependency=Join-Path $Root $(if($variant -eq 'standard'){'3rdparty\libplacebo\bin\libplacebo-360.dll'}else{'3rdparty\libplacebo-beta\bin\libplacebo-388.dll'})
        $actualDependency=Join-Path (Split-Path -Parent (Join-Path $Root $binary)) $(if($variant -eq 'standard'){'libplacebo-360.dll'}else{'libplacebo-388.dll'})
        if ((Get-FileHash -LiteralPath $expectedDependency).Hash -ne (Get-FileHash -LiteralPath $actualDependency).Hash) { throw "$variant tests did not use the selected bundled dependency file." }
    }
    $after=Get-VpSourceIdentity $Root
    if ($after.commit -ne $source.commit -or $after.fingerprint -ne $source.fingerprint -or $after.dirty -ne $source.dirty) { throw 'Sources changed during qualification; no runtime qualification is valid.' }
    if ($Lane -ne 'Qualification') {
        Assert-VpSameOutputInventory $binaries (Get-VpIterationOutputInventory $Root $Lane)
        $receipt=[ordered]@{schemaVersion=1;status=($Lane+'Passed');qualification=$false;lane=$Lane;source=$source;binaries=$binaries;builds=$builds;runs=$runs;seconds=[Math]::Round($totalTimer.Elapsed.TotalSeconds,3);shaderCacheDirectory=$env:VP_SHADER_CACHE_DIRECTORY;coldShaderCache=[bool]$coldCache;finished=(Get-Date).ToString('o')}
        $receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $runDirectory 'iteration.json') -Encoding UTF8
        $success=$true
        Write-Host "$Lane iteration passed: $runDirectory (not full runtime qualification)."
        return
    }
    Assert-VpSameOutputInventory $binaries (Get-VpQualifiedOutputInventory $Root)
    $fixtures=@(foreach($name in $plan.manifest.optionalPrivateFixtures) {
        $value=[Environment]::GetEnvironmentVariable([string]$name)
        if ($value) {
            if(-not (Test-Path -LiteralPath $value -PathType Leaf)){throw "Specified optional private fixture is missing: $name"}
            [ordered]@{environment=$name;status='supplied-not-proven-exercised';sha256=(Get-FileHash -LiteralPath $value).Hash;path=[IO.Path]::GetFullPath($value)}
        } else { [ordered]@{environment=$name;status='absent-not-claimed';sha256=$null;path=$null} }
    })
    $receipt=[ordered]@{schemaVersion=2;status='Passed';configuration='Release';platform='x64';source=$source;
        manifestHash=$plan.manifestHash;binaries=$binaries;builds=$builds;runs=$runs;optionalPrivateFixtures=$fixtures;seconds=[Math]::Round($totalTimer.Elapsed.TotalSeconds,3);shaderCacheDirectory=$env:VP_SHADER_CACHE_DIRECTORY;coldShaderCache=[bool]$coldCache;finished=(Get-Date).ToString('o')}
    $temporary=$receiptPath+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    $receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $receiptPath -Force
    $null=Assert-VpRuntimeQualification $Root $RequiredBinaryPath
    $success=$true
    Write-Host "Runtime qualified: $receiptPath"
} finally {
    $env:VP_CRITICAL_GATE_NONCE=$oldNonce;$env:VP_CRITICAL_GATE_ROOT=$oldRoot
    $env:VP_SHADER_CACHE_DIRECTORY=$oldCacheDirectory
    if (-not $success -and $locked) {
        if(Test-Path -LiteralPath $receiptPath){Remove-Item -LiteralPath $receiptPath -Force}
        if($runDirectory){[ordered]@{status='Failed';source=$source;builds=$builds;runs=$runs;finished=(Get-Date).ToString('o')}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $runDirectory 'failed.json') -Encoding UTF8}
    }
    if($locked){$mutex.ReleaseMutex()};$mutex.Dispose()
}
