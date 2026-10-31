[CmdletBinding()]
param(
    [string]$Root,
    [string]$MagewellSdkRoot,
    [string]$MSBuildPath,
    [string]$RequiredBinaryPath,
    [string]$Configuration='Release',
    [string]$Platform='x64'
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'runtime_qualification.ps1')
$Root=Get-VpQualificationRoot $Root
Assert-VpQualificationConfiguration $Configuration $Platform
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
$sha=[Security.Cryptography.SHA256]::Create()
try { $lockId=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Root.ToLowerInvariant())))).Replace('-','') }
finally { $sha.Dispose() }
$mutex=[Threading.Mutex]::new($false,('Local\VP-runtime-qualification-'+$lockId))
$locked=$false;$runs=@();$builds=@();$source=$null;$binaries=@();$runDirectory='';$success=$false
$oldNonce=$env:VP_CRITICAL_GATE_NONCE;$oldRoot=$env:VP_CRITICAL_GATE_ROOT
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
    foreach ($project in $projects) {
        Write-Host "Mandatory qualification: build $($project.name)"
        $arguments=@((Join-Path $Root $project.path))+$common+$project.extra
        & $MSBuildPath @arguments *> (Join-Path $runDirectory ($project.name+'-build.log'))
        $code=$LASTEXITCODE;$builds+=,[ordered]@{name=$project.name;project=$project.path;exit=$code}
        if ($code -ne 0) { throw "Critical companion build failed: $($project.name). See $runDirectory" }
    }
    $binaries=Get-VpQualifiedOutputInventory $Root
    foreach ($variant in @('standard','beta')) {
        $binary=if($variant -eq 'standard'){'x64\Release\VideoProcessor-Test.dll'}else{'x64\Release\tests-beta\VideoProcessor-TestBeta.dll'}
        $run=[ordered]@{variant=$variant;buildExit=0;testExit=$null;binary=$binary;trx=$null;trxSha256=$null;executed=0;native=0;actualClassic=0}
        $runs+=,$run
        $settings=Join-Path $runDirectory 'critical.runsettings'
        '<RunSettings><RunConfiguration><TestSessionTimeout>1200000</TestSessionTimeout></RunConfiguration></RunSettings>' | Set-Content -LiteralPath $settings -Encoding UTF8
        Write-Host "Mandatory qualification: execute all $($plan.nativeCount) native contracts and $($plan.actualCount) real Classic lifecycle tests [$variant]"
        Push-Location -LiteralPath (Split-Path -Parent (Join-Path $Root $binary))
        try {
            & $vstest (Join-Path $Root $binary) /Platform:x64 "/Settings:$settings" "/TestCaseFilter:$($plan.filter)" "/Logger:trx;LogFileName=$variant.trx" "/ResultsDirectory:$runDirectory" *> (Join-Path $runDirectory ($variant+'-tests.log'))
            $run.testExit=$LASTEXITCODE
        } finally { Pop-Location }
        if ($run.testExit -ne 0) { throw "Mandatory $variant native/actual-renderer contracts failed. See $runDirectory" }
        $trx=Join-Path $runDirectory ($variant+'.trx');$counts=Read-VpCriticalTrx $trx $plan
        $run.trx=$trx.Substring($Root.Length+1).Replace('\','/');$run.trxSha256=(Get-FileHash -LiteralPath $trx -Algorithm SHA256).Hash
        $run.executed=$counts.executed;$run.native=$counts.native;$run.actualClassic=$counts.actualClassic
        $expectedDependency=Join-Path $Root $(if($variant -eq 'standard'){'3rdparty\libplacebo\bin\libplacebo-360.dll'}else{'3rdparty\libplacebo-beta\bin\libplacebo-388.dll'})
        $actualDependency=Join-Path (Split-Path -Parent (Join-Path $Root $binary)) $(if($variant -eq 'standard'){'libplacebo-360.dll'}else{'libplacebo-388.dll'})
        if ((Get-FileHash -LiteralPath $expectedDependency).Hash -ne (Get-FileHash -LiteralPath $actualDependency).Hash) { throw "$variant tests did not use the selected bundled dependency file." }
    }
    $after=Get-VpSourceIdentity $Root
    if ($after.commit -ne $source.commit -or $after.fingerprint -ne $source.fingerprint -or $after.dirty -ne $source.dirty) { throw 'Sources changed during qualification; no runtime qualification is valid.' }
    Assert-VpSameOutputInventory $binaries (Get-VpQualifiedOutputInventory $Root)
    $fixtures=@(foreach($name in $plan.manifest.optionalPrivateFixtures) {
        $value=[Environment]::GetEnvironmentVariable([string]$name)
        if ($value) {
            if(-not (Test-Path -LiteralPath $value -PathType Leaf)){throw "Specified optional private fixture is missing: $name"}
            [ordered]@{environment=$name;status='supplied-not-proven-exercised';sha256=(Get-FileHash -LiteralPath $value).Hash;path=[IO.Path]::GetFullPath($value)}
        } else { [ordered]@{environment=$name;status='absent-not-claimed';sha256=$null;path=$null} }
    })
    $receipt=[ordered]@{schemaVersion=1;status='Passed';configuration='Release';platform='x64';source=$source;
        manifestHash=$plan.manifestHash;binaries=$binaries;builds=$builds;runs=$runs;optionalPrivateFixtures=$fixtures;finished=(Get-Date).ToString('o')}
    $temporary=$receiptPath+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    $receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $temporary -Encoding UTF8
    Move-Item -LiteralPath $temporary -Destination $receiptPath -Force
    $null=Assert-VpRuntimeQualification $Root $RequiredBinaryPath
    $success=$true
    Write-Host "Runtime qualified: $receiptPath"
} finally {
    $env:VP_CRITICAL_GATE_NONCE=$oldNonce;$env:VP_CRITICAL_GATE_ROOT=$oldRoot
    if (-not $success -and $locked) {
        if(Test-Path -LiteralPath $receiptPath){Remove-Item -LiteralPath $receiptPath -Force}
        if($runDirectory){[ordered]@{status='Failed';source=$source;builds=$builds;runs=$runs;finished=(Get-Date).ToString('o')}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $runDirectory 'failed.json') -Encoding UTF8}
    }
    if($locked){$mutex.ReleaseMutex()};$mutex.Dispose()
}
