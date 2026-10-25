param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$MagewellSdkRoot,
    [string]$VsInstallPath='C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional',
    [ValidateSet('standard','beta')][string[]]$Variants=@('standard','beta')
)
$ErrorActionPreference='Stop'
$vpRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$vpOutput=[IO.Path]::GetFullPath($OutputDirectory)
$vpMsbuild=Join-Path $VsInstallPath 'MSBuild\Current\Bin\MSBuild.exe'
$vpVstest=Join-Path $VsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe'
foreach($vpRequired in @($vpMsbuild,$vpVstest,(Join-Path $MagewellSdkRoot 'Include\LibMWCapture\MWCapture.h'))) {
    if(!(Test-Path -LiteralPath $vpRequired)){throw "Missing prerequisite: $vpRequired"}
}
if(Test-Path -LiteralPath (Join-Path $vpOutput 'motion-renderer-tests.json')){throw 'Choose a new results directory; existing receipts are preserved.'}
New-Item -ItemType Directory -Force -Path $vpOutput | Out-Null
$vpCommit=(& git -C $vpRoot rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0){throw 'Cannot identify test source'}
$vpDirty=@(& git -C $vpRoot status --porcelain).Count -ne 0
$vpClasses=@('MotionRendererTests','SourceUploadBatchTests','TemporalCleanPacketGpuTests',
    'RetainedEnvelopeSelectionGpuTests','ReconstructionMotionTests','SubtitleCutPasteTests','SubtitleCutPasteShaderTests','RendererDependencyTests')
$vpFilter=($vpClasses | ForEach-Object {"FullyQualifiedName~$_"}) -join '|'
$vpRuns=@()
try {
    foreach($vpVariant in ($Variants | Select-Object -Unique)) {
        $vpBuildArgs=@((Join-Path $vpRoot 'src\VideoProcessor-Test\VideoProcessor-Test.vcxproj.vcxproj'),
            '/t:Build','/m:4','/p:Configuration=Release','/p:Platform=x64',
            "/p:SolutionDir=$vpRoot\", "/p:MagewellSdkRoot=$MagewellSdkRoot", "/p:VPTestLibplacebo=$vpVariant",'/v:minimal','/nologo')
        & $vpMsbuild @vpBuildArgs *> (Join-Path $vpOutput "$vpVariant-build.log")
        $vpBuildExit=$LASTEXITCODE
        $vpRun=[ordered]@{variant=$vpVariant;buildExit=$vpBuildExit;testExit=$null;binary=$null;sha256=$null;dependencySha256=$null}
        $vpRuns+=,$vpRun
        if($vpBuildExit -ne 0){throw "$vpVariant build failed: $vpBuildExit"}
        $vpBin=Join-Path $vpRoot 'x64\Release'
        $vpName='VideoProcessor-Test.dll';$vpDependency='libplacebo-360.dll'
        if($vpVariant -eq 'beta'){$vpBin=Join-Path $vpBin 'tests-beta';$vpName='VideoProcessor-TestBeta.dll';$vpDependency='libplacebo-377.dll'}
        $vpRun.binary=Join-Path $vpBin $vpName
        $vpRun.sha256=(Get-FileHash -LiteralPath $vpRun.binary).Hash
        $vpRun.dependencySha256=(Get-FileHash -LiteralPath (Join-Path $vpBin $vpDependency)).Hash
        $vpDependencyFolder=if($vpVariant -eq 'beta'){'libplacebo-beta'}else{'libplacebo'}
        $vpExpectedDependency=Join-Path $vpRoot "3rdparty\$vpDependencyFolder\bin\$vpDependency"
        if($vpRun.dependencySha256 -ne (Get-FileHash -LiteralPath $vpExpectedDependency).Hash) {
            throw "$vpVariant output dependency differs from the selected bundled binary"
        }
        Push-Location -LiteralPath $vpBin
        try {
            & $vpVstest $vpRun.binary /Platform:x64 "/TestCaseFilter:$vpFilter" "/Logger:trx;LogFileName=$vpVariant.trx" "/ResultsDirectory:$vpOutput" *> (Join-Path $vpOutput "$vpVariant-tests.log")
            $vpRun.testExit=$LASTEXITCODE
        } finally {Pop-Location}
        if($vpRun.testExit -ne 0){throw "$vpVariant GPU/cue regression suite failed: $($vpRun.testExit)"}
        [xml]$vpTrx=Get-Content -LiteralPath (Join-Path $vpOutput "$vpVariant.trx") -Raw
        $vpResults=@($vpTrx.TestRun.Results.UnitTestResult)
        if(!$vpResults.Count -or @($vpResults | Where-Object {$_.outcome -ne 'Passed'}).Count) {
            throw "$vpVariant did not execute a fully passing regression set"
        }
        if(!($vpResults | Where-Object {$_.testName -like '*GpuTestsLoadTheSelectedRendererDependency*' -and $_.outcome -eq 'Passed'})) {
            throw "$vpVariant dependency identity regression did not pass"
        }
        foreach($vpClass in $vpClasses) {
            $vpClassIds=@($vpTrx.TestRun.TestDefinitions.UnitTest |
                Where-Object {$_.TestMethod.className -match [regex]::Escape($vpClass)} |
                ForEach-Object {$_.id})
            if(!($vpResults | Where-Object {$_.testId -in $vpClassIds -and $_.outcome -eq 'Passed'})) {
                throw "$vpVariant selected class did not execute: $vpClass"
            }
        }
        $vpRun['passed']=$vpResults.Count
    }
} finally {
    [pscustomobject]@{schemaVersion=1;sourceCommit=$vpCommit;sourceDirty=$vpDirty;configuration='Release';architecture='x64';
        classes=$vpClasses;runs=$vpRuns;finished=(Get-Date).ToString('o');
        scope='Controlled GPU/cue tests; physical playback, refresh and HDR display acceptance remain separate.'} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $vpOutput 'motion-renderer-tests.json')
}
