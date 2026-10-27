param([switch]$Deploy)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$sourceRoot='E:\codex\videoprocessor\vp-0218-adaptive-ar-20261027'
$deploymentRoot='C:\Videoprocessor\vp'
$diagnosticRoot='E:\codex\diagnostics\vp-0218-implementation-20261027'
$expectedCommit='ddf5d666803c29e0bdf7af310963b2695e382bc2'
function Hash([string]$path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
function Inside([string]$root,[string]$relative){
    $path=[IO.Path]::GetFullPath((Join-Path $root $relative))
    $prefix=[IO.Path]::GetFullPath($root).TrimEnd('\')+'\'
    if(-not $path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw "Path escapes intended root: $path"}
    return $path
}
$head=(& git -C $sourceRoot rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0 -or $head -ne $expectedCommit -or @(& git -C $sourceRoot status --porcelain).Count){throw 'Source identity or clean-check failed'}
$build=Get-Content -LiteralPath (Inside $diagnosticRoot 'candidate\release-build-result.json') -Raw | ConvertFrom-Json
if($build.sourceCommit -ne $expectedCommit -or $build.configuration -ne 'Release' -or $build.architecture -ne 'x64' -or $build.exitCode -ne 0){throw 'A matching successful x64 Release build is required'}
$manifest=Get-Content -LiteralPath (Inside $diagnosticRoot 'build-artifacts.json') -Raw | ConvertFrom-Json
if($manifest.sourceCommit -ne $expectedCommit){throw 'Artifact manifest source mismatch'}
foreach($artifact in $manifest.artifacts){
    $path=Inside $sourceRoot ('x64\Release\'+$artifact.relative)
    $record=Get-Content -LiteralPath ($path+'.runtime.json') -Raw | ConvertFrom-Json
    if((Hash $path) -ne $artifact.sha256 -or $record.sha256 -ne $artifact.sha256 -or $record.configuration -ne 'Release' -or $record.architecture -ne 'x64'){throw "Build artifact/receipt mismatch: $path"}
}
$qualificationPath=Inside $diagnosticRoot 'qualification.json'
& 'C:\Program Files\Python310\python.exe' (Inside $diagnosticRoot 'assemble-qualification.py') `
    --source-root $sourceRoot --expected-commit $expectedCommit `
    --tested-commit 'e211bdb929219ea6e60d2e9390b0cf90cbabef8a' `
    --baseline (Inside $diagnosticRoot 'baseline\baseline-full.trx') --expected-total 3433 `
    --runs (Inside $diagnosticRoot 'candidate\core-full.trx') (Inside $diagnosticRoot 'candidate\gpu-full.trx') (Inside $diagnosticRoot 'candidate\final-repairs.trx') `
    --output $qualificationPath
if($LASTEXITCODE -ne 0){throw 'The complete effective test inventory has not passed with verified source/build provenance'}
$qualification=Get-Content -LiteralPath $qualificationPath -Raw | ConvertFrom-Json
if($qualification.status -ne 'passed' -or $qualification.sourceCommit -ne $expectedCommit -or $qualification.passed -ne 3433){throw 'Test qualification mismatch'}
$testCounts=@(foreach($run in $qualification.runsInEffectiveOrder){
    [pscustomobject]@{file=$run.path;passed=[int]$run.counters.passed;failed=[int]$run.counters.failed;total=[int]$run.counters.total;sha256=$run.sha256}
})
$qualifiedTests=[pscustomobject]@{status=$qualification.status;uniquePassed=$qualification.passed;uniqueTotal=$qualification.uniqueTests;file=$qualificationPath;sha256=(Hash $qualificationPath);originalFailuresPreserved=$true;productionSourceUnchangedSinceBroadTests=$qualification.productionSourceUnchangedSinceBroadTests}
$mapping=@(
    @('VideoProcessor-GUI.exe','VideoProcessor.exe'),
    @('VideoProcessor-GUI.exe.runtime.json','VideoProcessor.exe.runtime.json'),
    @('VideoProcessor-GUI.pdb','VideoProcessor-GUI.pdb'),
    @('vprenderer\VideoProcessorVPRenderer.dll','vprenderer\VideoProcessorVPRenderer.dll'),
    @('vprenderer\VideoProcessorVPRenderer.dll.runtime.json','vprenderer\VideoProcessorVPRenderer.dll.runtime.json'),
    @('vprenderer\VideoProcessorVPRenderer.pdb','vprenderer\VideoProcessorVPRenderer.pdb'),
    @('vprenderer-beta\VideoProcessorVPRendererBeta.dll','vprenderer-beta\VideoProcessorVPRendererBeta.dll'),
    @('vprenderer-beta\VideoProcessorVPRendererBeta.dll.runtime.json','vprenderer-beta\VideoProcessorVPRendererBeta.dll.runtime.json'),
    @('vprenderer-beta\VideoProcessorVPRendererBeta.pdb','vprenderer-beta\VideoProcessorVPRendererBeta.pdb')
)
$plan=@(foreach($entry in $mapping){
    $source=Inside $sourceRoot ('x64\Release\'+$entry[0]); $target=Inside $deploymentRoot $entry[1]
    if(-not(Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing artifact: $source"}
    [pscustomobject]@{source=$source;relative=$entry[1];target=$target;sourceSha256=(Hash $source);existed=(Test-Path -LiteralPath $target -PathType Leaf)}
})
foreach($folder in @('vprenderer','vprenderer-beta')){
    foreach($dll in Get-ChildItem -LiteralPath (Inside $sourceRoot ('x64\Release\'+$folder)) -File -Filter '*.dll' | Where-Object { $_.Name -notmatch '^VideoProcessorVPRenderer' }){
        $installed=Inside $deploymentRoot ($folder+'\'+$dll.Name)
        if(-not(Test-Path -LiteralPath $installed) -or (Hash $installed) -ne (Hash $dll.FullName)){throw "Dependency mismatch: $installed"}
    }
}
if(-not $Deploy){[pscustomobject]@{status='preflight-passed';sourceCommit=$head;files=$plan.Count;qualification=$qualifiedTests;tests=$testCounts} | ConvertTo-Json -Depth 4; exit 0}
$principal=[Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()

$playerPath=Inside $deploymentRoot 'VideoProcessor.exe'
$players=@(Get-CimInstance Win32_Process -Filter "Name='VideoProcessor.exe'")
if(@($players | Where-Object {-not $_.ExecutablePath}).Count){throw 'Cannot establish player path; refusing to stop an unidentified process'}
$players=@($players | Where-Object { $_.ExecutablePath -ieq $playerPath })
if($players.Count -gt 1){throw 'Multiple deployment player processes require an explicit restart plan'}
if($players.Count -gt 0 -and -not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Elevation is required to replace the running player'}
$restartArguments=''
if($players.Count){
    $command=$players[0].CommandLine.Trim()
    $quoted='"'+$playerPath+'"'
    if($command.StartsWith($quoted,[StringComparison]::OrdinalIgnoreCase)){$restartArguments=$command.Substring($quoted.Length).Trim()}
    elseif($command.StartsWith($playerPath,[StringComparison]::OrdinalIgnoreCase)){$restartArguments=$command.Substring($playerPath.Length).Trim()}
    else{throw 'Cannot preserve existing player command line safely'}
}
function StartDeploymentPlayer {
    if($restartArguments){return Start-Process -FilePath $playerPath -ArgumentList $restartArguments -WorkingDirectory $deploymentRoot -PassThru}
    return Start-Process -FilePath $playerPath -WorkingDirectory $deploymentRoot -PassThru
}
$backupRoot=Inside $deploymentRoot ('deployment-backup-vp0218-'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-ddf5d666')
if(Test-Path -LiteralPath $backupRoot){throw 'Backup path already exists'}
New-Item -ItemType Directory -Path $backupRoot | Out-Null
$prior=@(foreach($item in $plan){
    $backup=Inside $backupRoot $item.relative
    $oldHash=$null
    if($item.existed){
        $oldHash=Hash $item.target
        New-Item -ItemType Directory -Path (Split-Path -Parent $backup) -Force | Out-Null
        Copy-Item -LiteralPath $item.target -Destination $backup
        if((Hash $backup) -ne $oldHash){throw "Backup mismatch: $backup"}
    }
    [pscustomobject]@{source=$item.source;target=$item.target;backup=$backup;existed=$item.existed;beforeSha256=$oldHash;expectedSha256=$item.sourceSha256}
})
$replaced=[Collections.Generic.List[object]]::new()
$playerStopped=$false
$started=$null
try{
    foreach($player in $players){
        Stop-Process -Id $player.ProcessId -ErrorAction Stop
        $playerStopped=$true
        $remaining=Get-Process -Id $player.ProcessId -ErrorAction SilentlyContinue
        if($remaining -and -not $remaining.WaitForExit(10000)){throw 'Player did not stop; replacement cancelled'}
    }
    $configs=@(foreach($name in @('VideoProcessor.cfg','VideoProcessor.state')){
        $path=Inside $deploymentRoot $name; $backup=Inside $backupRoot $name
        if(Test-Path -LiteralPath $path){
            Copy-Item -LiteralPath $path -Destination $backup
            $hash=Hash $path
            if((Hash $backup) -ne $hash){throw "Configuration backup mismatch: $path"}
            [pscustomobject]@{path=$path;backup=$backup;beforeSha256=$hash;afterCopySha256=$null;unchangedBeforeRestart=$false;editedByDeployment=$false}
        }
    })
    Get-Content -LiteralPath (Inside $deploymentRoot 'logs\vp.log') -Tail 1000 | Set-Content -LiteralPath (Inside $backupRoot 'predeployment-runtime-tail.log') -Encoding UTF8
    foreach($item in $prior){
        $replaced.Add($item) # Include partial-copy failures in rollback.
        Copy-Item -LiteralPath $item.source -Destination $item.target -Force
        if((Hash $item.target) -ne $item.expectedSha256){throw "Deployed hash mismatch: $($item.target)"}
    }
    foreach($config in $configs){$config.afterCopySha256=Hash $config.path;$config.unchangedBeforeRestart=$config.afterCopySha256 -eq $config.beforeSha256;if(-not $config.unchangedBeforeRestart){throw 'Configuration changed during replacement'}}
    $receipt=[ordered]@{sourceCommit=$head;configuration='Release';architecture='x64';status='deployed-hashes-verified';timeUtc=[DateTimeOffset]::UtcNow.ToString('o');backupDirectory=$backupRoot;pullRequest='https://github.com/billslack2/videoprocessor/pull/213';files=$prior;qualification=$qualifiedTests;tests=$testCounts;configurationFiles=$configs;configurationEdits=@();playerWasRunning=($players.Count -gt 0);startedProcessId=$null;runtimeVerification='pending'}
    $receiptPath=Inside $diagnosticRoot 'deployment-receipt.json'
    $receipt | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
    if($players.Count -gt 0){$started=StartDeploymentPlayer; $receipt.startedProcessId=$started.Id}
    $receipt | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
    Copy-Item -LiteralPath $receiptPath -Destination (Inside $backupRoot 'deployment-receipt.json')
    $receipt | ConvertTo-Json -Depth 7
}catch{
    $failure=$_
    $rollbackErrors=[Collections.Generic.List[string]]::new()
    $candidateStopped=$true
    if($started){
        try{
            $active=Get-Process -Id $started.Id -ErrorAction SilentlyContinue
            if($active){Stop-Process -Id $active.Id -ErrorAction Stop; if(-not $active.WaitForExit(10000)){throw 'Candidate did not exit'}}
        }catch{$candidateStopped=$false;$rollbackErrors.Add($_.Exception.Message)}
    }
    if($candidateStopped){
        foreach($item in $replaced){
            try{
                if($item.existed){
                    Copy-Item -LiteralPath $item.backup -Destination $item.target -Force
                    if((Hash $item.target) -ne $item.beforeSha256){throw "Rollback hash mismatch: $($item.target)"}
                }elseif(Test-Path -LiteralPath $item.target){Remove-Item -LiteralPath $item.target -Force}
            }catch{$rollbackErrors.Add($_.Exception.Message)}
        }
    }
    $restoredProcessId=$null
    if($rollbackErrors.Count -eq 0 -and $playerStopped){
        try{
            $remainingPlayers=@(Get-CimInstance Win32_Process -Filter "Name='VideoProcessor.exe'")
            if(@($remainingPlayers | Where-Object {-not $_.ExecutablePath}).Count){throw 'Cannot verify process identity before rollback restart'}
            $sameDeployment=@($remainingPlayers | Where-Object {$_.ExecutablePath -ieq $playerPath})
            if($sameDeployment.Count){
                $restoredProcessId=$sameDeployment[0].ProcessId
                throw 'Original deployment process remains; refusing duplicate rollback restart'
            }
            $restored=StartDeploymentPlayer;$restoredProcessId=$restored.Id
        }catch{$rollbackErrors.Add($_.Exception.Message)}
    }
    [ordered]@{status=$(if($rollbackErrors.Count){'rollback-incomplete'}else{'runtime-rolled-back'});sourceCommit=$head;error=$failure.Exception.Message;backupDirectory=$backupRoot;rollbackErrors=@($rollbackErrors);restoredProcessId=$restoredProcessId} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Inside $diagnosticRoot 'deployment-failure.json') -Encoding UTF8
    throw $failure
}