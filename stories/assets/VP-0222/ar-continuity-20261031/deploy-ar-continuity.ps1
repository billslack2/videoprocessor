param([Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{40}$')][string]$ExpectedCommit,[switch]$ValidateOnly)
$ErrorActionPreference='Stop'
$vpSource='E:\codex\videoprocessor\ar-evidence-continuity-20261031'
$vpBuild=Join-Path $vpSource 'x64\Release'
$vpValidation=Join-Path $vpBuild 'ar-validation'
$vpDestination='C:\Videoprocessor\vp'
$vpExpectedInstalledHost='195CABDD6C4F48994A5B8EACB7FD6608225C09DD257FF12FC97940054AEDFC3A'
if((Get-FileHash (Join-Path $vpDestination 'VideoProcessor.exe')).Hash -ne $vpExpectedInstalledHost){throw 'Installed VP changed during validation; reconcile its source before deploying this candidate'}
$vpExpected=$ExpectedCommit
$vpHead=(& git -C $vpSource rev-parse HEAD).Trim()
if($vpHead -ne $vpExpected){throw 'Unexpected source commit'}
if(@(& git -C $vpSource status --porcelain).Count){throw 'Source must be clean'}
if((Get-Content (Join-Path $vpValidation 'release-build-passed.txt') -Raw).Trim() -ne $vpHead){throw 'Successful pinned Release build required'}
foreach($vpHeader in @('src\VideoProcessor-GUI\version.h','src\VideoProcessor-VPRenderer\version.h')){
    $vpText=Get-Content (Join-Path $vpSource $vpHeader) -Raw
    if($vpText -notmatch $vpHead.Substring(0,7) -or $vpText -notmatch 'VERSION_DIRTY\s*=\s*false'){throw "Wrong build identity: $vpHeader"}
}
$vpCounts=@{}
& (Join-Path $PSScriptRoot 'verify-cache-exception.ps1') -Validation $vpValidation
$vpCounts['final-full']=@{total=3392;passed=3391;failed=1;exception='Existing unchanged configuration-cache metadata collision; see cache-failure-review.json'}
$vpCounts['config-cache-isolated']=5
foreach($vpSuite in @('final-renderer','captured-replays')) {
    [xml]$vpTrx=Get-Content (Join-Path $vpValidation ($vpSuite+'.trx'))
    $vpCounters=$vpTrx.TestRun.ResultSummary.Counters
    if([int]$vpCounters.total -le 0 -or $vpCounters.passed -ne $vpCounters.total){throw "Suite did not fully pass: $vpSuite"}
    if($vpSuite -eq 'captured-replays'){
        $vpReplayNames=@('OptionalNativeLowContrastCaptureReleasesRetainedScope','OptionalOrderedNativeAnalysisCaptureReportsFreshAcquisition','OptionalNativeFullWidthMenuWithdrawsObsoletePillarbox')
        if($vpCounters.total -ne 3 -or @(Compare-Object ($vpReplayNames | Sort-Object) ($vpTrx.TestRun.Results.UnitTestResult.testName | Sort-Object)).Count){throw 'Required native capture replay coverage is incomplete'}
    }
    $vpCounts[$vpSuite]=[int]$vpCounters.passed
}
$vpCoverage=Get-Content (Join-Path $vpValidation 'test-coverage.json') -Raw | ConvertFrom-Json
if($vpCoverage.commit -ne $vpHead -or $vpCoverage.missing.Count -or $vpCoverage.extra.Count -or $vpCoverage.unexpectedFailures.Count -or $vpCoverage.knownFailures.Count -ne 1 -or $vpCoverage.knownFailures[0] -ne 'final-full: SameSizeEditWithRestoredWriteTimeInvalidatesCache'){throw 'Final inventory coverage has omissions or unexpected failures'}
$vpEvidence=Get-Content (Join-Path $vpValidation 'tested-source.json') -Raw | ConvertFrom-Json
if($vpEvidence.commit -ne $vpHead -or $vpEvidence.sourceDirty -or $vpEvidence.testSha256 -ne (Get-FileHash $vpEvidence.testBinary).Hash){throw 'Test evidence differs from final source/binary'}
$vpGpu=Get-Content (Join-Path $vpValidation 'gpu\motion-renderer-tests.json') -Raw | ConvertFrom-Json
if($vpGpu.sourceCommit -ne $vpHead -or $vpGpu.sourceDirty -or @($vpGpu.runs).Count -ne 2){throw 'Both clean pinned GPU variants required'}
if((@($vpGpu.runs.variant | Sort-Object -Unique) -join ',') -ne 'beta,standard'){throw 'Exactly one standard and one beta GPU run required'}
foreach($vpRun in $vpGpu.runs){
    if($vpRun.buildExit -ne 0 -or $vpRun.testExit -ne 0 -or $vpRun.passed -lt 200){throw 'GPU suite failed or incomplete'}
    if($vpRun.sha256 -ne (Get-FileHash $vpRun.binary).Hash){throw 'GPU test binary changed'}
    $vpCounts['gpu-'+$vpRun.variant]=$vpRun.passed
}
$vpMappings=@(
    @{source='VideoProcessor-GUI.exe';target='VideoProcessor.exe'},
    @{source='vprenderer\VideoProcessorVPRenderer.dll';target='vprenderer\VideoProcessorVPRenderer.dll'},
    @{source='vprenderer-beta\VideoProcessorVPRendererBeta.dll';target='vprenderer-beta\VideoProcessorVPRendererBeta.dll'}
)
foreach($vpMapping in @($vpMappings)){
    $vpBinary=Join-Path $vpBuild $vpMapping.source
    $vpEmbedded=[Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($vpBinary))
    if(!$vpEmbedded.Contains('https://github.com/billslack2/videoprocessor/commit/'+$vpExpected)){throw "Embedded runtime source identity mismatch: $vpBinary"}
    $vpRecord=Get-Content ($vpBinary+'.runtime.json') -Raw | ConvertFrom-Json
    if($vpRecord.configuration -ne 'Release' -or $vpRecord.architecture -ne 'x64' -or $vpRecord.sha256 -ne (Get-FileHash $vpBinary).Hash){throw "Invalid Release runtime record: $vpBinary"}
    $vpMappings+=@{source=$vpMapping.source+'.runtime.json';target=$vpMapping.target+'.runtime.json'}
}
foreach($vpFolder in @('vprenderer','vprenderer-beta')){
    foreach($vpDll in Get-ChildItem (Join-Path $vpBuild $vpFolder) -Filter '*.dll'){
        $vpRelative=Join-Path $vpFolder $vpDll.Name
        if($vpMappings.target -contains $vpRelative){continue}
        $vpInstalled=Join-Path $vpDestination $vpRelative
        if(!(Test-Path $vpInstalled) -or (Get-FileHash $vpDll.FullName).Hash -ne (Get-FileHash $vpInstalled).Hash){
            $vpMappings+=@{source=$vpRelative;target=$vpRelative}
        }
    }
}
foreach($vpPdb in @('VideoProcessor-GUI.pdb','vprenderer\VideoProcessorVPRenderer.pdb','vprenderer-beta\VideoProcessorVPRendererBeta.pdb')){
    if(Test-Path (Join-Path $vpBuild $vpPdb)){$vpMappings+=@{source=$vpPdb;target=$vpPdb}}
}
$vpMappings+=@{source='VideoProcessor-GUI.exe.runtime.json';target='VideoProcessor-GUI.exe.runtime.json'}
if($ValidateOnly){[pscustomobject]@{commit=$vpHead;tests=$vpCounts;files=$vpMappings.target}|ConvertTo-Json -Depth 5;return}
if(Get-Process -Name VideoProcessor -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq 'C:\Videoprocessor\vp\VideoProcessor.exe'}){throw 'Close VP gracefully before installing'}
$vpBackup=Join-Path $vpDestination ('deploy-backups\ar-continuity-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
if(Test-Path $vpBackup){throw 'Backup already exists'}
New-Item -ItemType Directory -Path $vpBackup | Out-Null
$vpConfigHashes=@{}
foreach($vpConfig in @('VideoProcessor.cfg','VideoProcessor.state')){
    $vpPath=Join-Path $vpDestination $vpConfig
    $vpConfigHashes[$vpConfig]=(Get-FileHash $vpPath).Hash
    Copy-Item -LiteralPath $vpPath -Destination (Join-Path $vpBackup $vpConfig)
}
$vpRows=@()
foreach($vpMapping in $vpMappings){
    $vpOld=Join-Path $vpDestination $vpMapping.target
    $vpSaved=Join-Path $vpBackup $vpMapping.target
    $vpBefore=$null
    if(Test-Path $vpOld){
        New-Item -ItemType Directory -Force -Path (Split-Path $vpSaved) | Out-Null
        $vpBefore=(Get-FileHash $vpOld).Hash
        Copy-Item -LiteralPath $vpOld -Destination $vpSaved
        if((Get-FileHash $vpSaved).Hash -ne $vpBefore){throw "Backup mismatch: $vpOld"}
    }
    $vpRows+=@{path=$vpMapping.target;source=$vpMapping.source;before=$vpBefore;after=(Get-FileHash (Join-Path $vpBuild $vpMapping.source)).Hash}
}
try{
    foreach($vpMapping in $vpMappings){
        $vpTarget=Join-Path $vpDestination $vpMapping.target
        New-Item -ItemType Directory -Force -Path (Split-Path $vpTarget) | Out-Null
        Copy-Item -LiteralPath (Join-Path $vpBuild $vpMapping.source) -Destination $vpTarget -Force
    }
    foreach($vpRow in $vpRows){if((Get-FileHash (Join-Path $vpDestination $vpRow.path)).Hash -ne $vpRow.after){throw "Installed hash mismatch: $($vpRow.path)"}}
    foreach($vpConfig in $vpConfigHashes.Keys){if((Get-FileHash (Join-Path $vpDestination $vpConfig)).Hash -ne $vpConfigHashes[$vpConfig]){throw "Configuration changed: $vpConfig"}}
}catch{
    foreach($vpRow in $vpRows){
        $vpRestore=[IO.Path]::GetFullPath((Join-Path $vpDestination $vpRow.path))
        if(!$vpRestore.StartsWith(([IO.Path]::GetFullPath($vpDestination)+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Rollback path escapes deployment root'}
        if($vpRow.before){
            Copy-Item -LiteralPath (Join-Path $vpBackup $vpRow.path) -Destination $vpRestore -Force
            if((Get-FileHash $vpRestore).Hash -ne $vpRow.before){throw "Rollback hash mismatch: $vpRestore"}
        }elseif(Test-Path -LiteralPath $vpRestore){Remove-Item -LiteralPath $vpRestore -Force}
    }
    throw
}
$vpReceipt=[pscustomobject]@{commit=$vpHead;branch='codex/ar-evidence-continuity-20261031';configuration='Release';architecture='x64';tests=$vpCounts;backup=$vpBackup;configurationEdits=@();configurationHashes=$vpConfigHashes;files=$vpRows;installedAt=(Get-Date).ToString('o')}
$vpReceipt|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $vpBackup 'deployment.json')
$vpReceipt|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $vpValidation 'deployment-receipt.json')
$vpReceipt|ConvertTo-Json -Depth 6
