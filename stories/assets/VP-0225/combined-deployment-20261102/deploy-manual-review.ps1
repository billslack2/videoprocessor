$ErrorActionPreference='Stop'
$sourceRoot='E:\codex\videoprocessor\combined-ar-stutter-20261102'
$buildRoot=Join-Path $sourceRoot 'x64\Release'
$deployRoot='C:\Videoprocessor\vp'
$evidenceRoot=$PSScriptRoot
$expectedCommit='53058498acf3462dd698c3b5b4ea1c0467025769'
if((& git -C $sourceRoot rev-parse HEAD).Trim() -ne $expectedCommit){throw 'Source commit changed'}
if(@(& git -C $sourceRoot status --porcelain).Count){throw 'Source is not clean'}
$runDir=Join-Path $sourceRoot 'artifacts\qualification\runs\20261102-221547-060b707b'
foreach($pair in @(@('host-build.log','VideoProcessor-GUI.exe'),@('standard-renderer-build.log','VideoProcessorVPRenderer.dll'),@('beta-renderer-build.log','VideoProcessorVPRendererBeta.dll'))) {
    $log=Get-Content (Join-Path $runDir $pair[0]) -Raw
    if($log -notmatch ('-> .*'+[regex]::Escape($pair[1])) -or $log -match ': error [A-Z]+[0-9]+:'){throw ('No successful Release build evidence: '+$pair[0])}
}
foreach($relative in @('VideoProcessor-GUI.exe','vprenderer\VideoProcessorVPRenderer.dll','vprenderer-beta\VideoProcessorVPRendererBeta.dll')) {
    $binary=Join-Path $buildRoot $relative;$record=Get-Content ($binary+'.runtime.json') -Raw | ConvertFrom-Json
    if($record.configuration -ne 'Release' -or $record.architecture -ne 'x64' -or $record.sha256 -ne (Get-FileHash $binary).Hash){throw ('Release runtime record mismatch: '+$relative)}
}
if(Get-Process VideoProcessor -ErrorAction SilentlyContinue){throw 'The deployed player is running; preserve it until shutdown is coordinated'}
$prior=Get-Content 'C:\Users\bslac\vp\story-tracker\stories\assets\VP-0222\ar-design-review-20261101\deployment-788554a1.json' -Raw | ConvertFrom-Json
$files=@(foreach($item in $prior.files){
    $source=[IO.Path]::GetFullPath((Join-Path $buildRoot $item.source))
    $target=[IO.Path]::GetFullPath((Join-Path $deployRoot $item.target))
    if(!$source.StartsWith($buildRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or
       !$target.StartsWith($deployRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Deployment path escapes its root'}
    $sha=(Get-FileHash -LiteralPath $source).Hash
    $before=(Get-FileHash -LiteralPath $target).Hash
    if($sha -ne $before){$lease=[IO.File]::Open($target,'Open','ReadWrite','None');$lease.Dispose()}
    [ordered]@{source=$item.source;target=$item.target;sha256=$sha;before=$before;action=$(if($sha -eq $before){'already-matching'}else{'copy'})}
})
if($files.Count -ne 28){throw 'Unexpected paired runtime inventory'}
$backup=Join-Path $deployRoot ('backups\combined-ar-stutter-53058498-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup | Out-Null
foreach($item in $files){$target=Join-Path $backup $item.target;New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null;Copy-Item -LiteralPath (Join-Path $deployRoot $item.target) -Destination $target}
$configHashes=@{}
foreach($name in @('VideoProcessor.cfg','VideoProcessor.state')){$path=Join-Path $deployRoot $name;$configHashes[$name]=(Get-FileHash $path).Hash;Copy-Item -LiteralPath $path -Destination (Join-Path $backup $name)}
# User explicitly requested deployment before full qualification; only successfully built Release outputs are copied.
if((& git -C $sourceRoot rev-parse HEAD).Trim() -ne $expectedCommit -or @(& git -C $sourceRoot status --porcelain).Count){throw 'Source changed before copying'}
foreach($item in $files){if($item.action -eq 'copy'){Copy-Item -LiteralPath (Join-Path $buildRoot $item.source) -Destination (Join-Path $deployRoot $item.target) -Force}}
foreach($item in $files){if((Get-FileHash -LiteralPath (Join-Path $deployRoot $item.target)).Hash -ne $item.sha256){throw ('Deployment hash mismatch: '+$item.target)}}
foreach($name in $configHashes.Keys){if((Get-FileHash -LiteralPath (Join-Path $deployRoot $name)).Hash -ne $configHashes[$name]){throw ('Configuration changed during deployment: '+$name)}}
$receipt=[ordered]@{sourceCommit=$expectedCommit;configuration='Release';architecture='x64';deploymentStatus='ManualReviewQualificationPending';fullQualification=$false;
    backup=$backup;configurationEdits=@();configurationHashesBefore=$configHashes;configurationPreservedBeforeRestart=$true;
    authorization='User requested deploy first while combined qualification continues';motionTests=@();files=$files;deployedAt=(Get-Date).ToString('o')}
$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'deployment-combined.json') -Encoding utf8
$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $backup 'deployment.json') -Encoding utf8
Write-Output ('MANUAL_REVIEW_DEPLOYMENT_VERIFIED source='+$expectedCommit+' files='+$files.Count+' backup='+$backup)

