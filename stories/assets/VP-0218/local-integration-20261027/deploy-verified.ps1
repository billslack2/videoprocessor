param([switch]$Deploy)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$source='E:\codex\videoprocessor\vp-0218-local-integration-20261027'
$out='E:\codex\diagnostics\vp-0218-local-integration-20261027'
$release=Join-Path $source 'x64\Release'
$target='C:\Videoprocessor\vp'
$commit='f4219a7aa679c57f1f6d637cb08406b9c2b4b97b'
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}
function ReadJson([string]$name){Get-Content -LiteralPath (Join-Path $out $name) -Raw|ConvertFrom-Json}
function Inside([string]$base,[string]$rel){$p=[IO.Path]::GetFullPath((Join-Path $base $rel));if(-not $p.StartsWith([IO.Path]::GetFullPath($base).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path outside deployment scope'};return $p}
if((& git -C $source rev-parse HEAD).Trim() -ne $commit -or @(& git -C $source status --porcelain).Count){throw 'Clean pinned integration required'}
$build=ReadJson 'build-result.json';$test=ReadJson 'native-test-result.json';$cfgTest=ReadJson 'config-test-result.json'
if($build.exitCode -ne 0 -or $build.sourceCommit -ne $commit -or $build.configuration -ne 'Release' -or $build.architecture -ne 'x64'){throw 'Successful exact Release x64 build required'}
if($test.exitCode -ne 0 -or $test.sourceCommit -ne $commit -or (Hash (Join-Path $release 'VideoProcessor-Test.dll')) -ne $test.testAssemblySha256){throw 'Exact native suite must pass'}
if($cfgTest.exitCode -ne 0 -or $cfgTest.sourceCommit -ne $commit -or (Hash (Join-Path $release 'VideoProcessorConfigTests.exe')) -ne $cfgTest.testExecutableSha256){throw 'Exact Config guard test must pass'}
[xml]$trx=Get-Content -LiteralPath (Join-Path $out 'combined-native-full.trx')
$results=@($trx.SelectNodes('//*[local-name()="UnitTestResult"]'))
if($results.Count -lt 3433 -or @($results|Where-Object {$_.outcome -ne 'Passed'}).Count){throw 'Full native results missing or not all passed'}
$counters=$trx.SelectSingleNode('//*[local-name()="ResultSummary"]/*[local-name()="Counters"]')
if([int]$counters.total -ne $results.Count -or [int]$counters.passed -ne $results.Count -or [int]$counters.failed -ne 0 -or [int]$counters.notExecuted -ne 0){throw 'Full run counters do not reconcile'}
$qualified=ReadJson 'qualification.json'
if($qualified.sourceCommit -ne $commit -or $qualified.status -ne 'passed' -or $qualified.total -ne $results.Count -or $qualified.missingPriorTests.Count -ne 0 -or $qualified.trxSha256 -ne (Hash (Join-Path $out 'combined-native-full.trx'))){throw 'Complete prior/current inventory qualification required'}
$baseArtifacts=ReadJson 'installed-base-artifacts.json'
foreach($baseFile in $baseArtifacts){if((Hash (Inside $target $baseFile.relative)) -ne $baseFile.expectedSha256){throw "Installed runtime advanced: $($baseFile.relative)"}}

$bins=@(@('VideoProcessor-GUI.exe','VideoProcessor.exe'),@('vprenderer\VideoProcessorVPRenderer.dll','vprenderer\VideoProcessorVPRenderer.dll'),@('vprenderer-beta\VideoProcessorVPRendererBeta.dll','vprenderer-beta\VideoProcessorVPRendererBeta.dll'),@('VideoProcessorConfig.exe','config\VideoProcessorConfig.exe'),@('VideoProcessorConfigDiscovery.dll','config\VideoProcessorConfigDiscovery.dll'))
$mapping=[Collections.Generic.List[object]]::new()
foreach($pair in $bins){
 $file=Inside $release $pair[0];$record=Get-Content -LiteralPath ($file+'.runtime.json') -Raw|ConvertFrom-Json
 if($record.configuration -ne 'Release' -or $record.architecture -ne 'x64' -or $record.sha256 -ne (Hash $file)){throw "Invalid runtime record: $file"}
 $mapping.Add(@($pair[0],$pair[1]));$mapping.Add(@(($pair[0]+'.runtime.json'),($pair[1]+'.runtime.json')))
 $symbol=[IO.Path]::ChangeExtension($pair[0],'.pdb');if(Test-Path -LiteralPath (Inside $release $symbol)){$destSymbol=if($pair[0] -eq 'VideoProcessor-GUI.exe'){'VideoProcessor-GUI.pdb'}else{[IO.Path]::ChangeExtension($pair[1],'.pdb')};$mapping.Add(@($symbol,$destSymbol))}
}
$dependencies=[Collections.Generic.List[object]]::new()
foreach($folder in @('vprenderer','vprenderer-beta')){foreach($f in Get-ChildItem -LiteralPath (Join-Path $release $folder) -File -Filter '*.dll'|Where-Object {$_.Name -notmatch '^VideoProcessorVPRenderer'}){$rel=$folder+'\'+$f.Name;$dest=Inside $target $rel;if(-not(Test-Path -LiteralPath $dest) -or (Hash $f.FullName) -ne (Hash $dest)){throw "Renderer dependency differs: $rel"};$dependencies.Add([ordered]@{relative=$rel;sha256=(Hash $dest)})}}
foreach($f in Get-ChildItem -LiteralPath $release -File|Where-Object {$_.Name -like 'Qt6*.dll' -or $_.Name -eq 'opengl32sw.dll'}){$dest=Inside $target ('config\'+$f.Name);if(-not(Test-Path -LiteralPath $dest) -or (Hash $f.FullName) -ne (Hash $dest)){throw "Config dependency differs: $($f.Name)"};$dependencies.Add([ordered]@{relative=('config\'+$f.Name);sha256=(Hash $dest)})}
$files=@(foreach($pair in $mapping){$from=Inside $release $pair[0];$dest=Inside $target $pair[1];[pscustomobject]@{source=$from;target=$dest;relative=$pair[1];expectedSha256=(Hash $from);existed=(Test-Path -LiteralPath $dest);beforeSha256=$(if(Test-Path -LiteralPath $dest){Hash $dest}else{$null});backup=$null}})
$plan=[ordered]@{sourceCommit=$commit;localBase='f22e22c4f0dcd9be87c26189fc18a83a798c9a26';configuration='Release';architecture='x64';nativePassed=$results.Count;configTest=$cfgTest;files=$files;retainedDependencies=@($dependencies);configEdits=@();preparedUtc=[DateTimeOffset]::UtcNow.ToString('o')}
$plan|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $out 'deployment-plan.json') -Encoding utf8
if(-not $Deploy){Write-Output "Preflight passed: $($files.Count) files; $($results.Count) native tests; Config guard pass.";exit 0}
if(@(Get-CimInstance Win32_Process|Where-Object {$_.Name -in @('VideoProcessor.exe','VideoProcessorConfig.exe')}).Count){throw 'VP and Config must exit normally before replacement; no process was stopped'}
$backup=Inside $target ('backups\ar-local-f4219a7a-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup | Out-Null
$configs=@(foreach($name in @('VideoProcessor.cfg','VideoProcessor.state')){$p=Inside $target $name;$b=Inside $backup $name;Copy-Item -LiteralPath $p -Destination $b;if((Hash $p) -ne (Hash $b)){throw 'Config backup mismatch'};[pscustomobject]@{file=$name;sha256=(Hash $p);backup=$b;edited=$false}})
foreach($f in $files){$f.backup=Inside $backup $f.relative;if($f.existed){New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($f.backup)) -Force|Out-Null;Copy-Item -LiteralPath $f.target -Destination $f.backup;if((Hash $f.backup) -ne $f.beforeSha256 -or (Hash $f.target) -ne $f.beforeSha256){throw 'Runtime changed during backup'}}}
foreach($f in $files){
 $staged=Inside $backup ('staged\'+$f.relative)
 New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($staged)) -Force|Out-Null
 Copy-Item -LiteralPath $f.source -Destination $staged
 if((Hash $staged) -ne $f.expectedSha256){throw 'Staged payload mismatch'}
 $f|Add-Member -NotePropertyName staged -NotePropertyValue $staged
}
if(@(Get-CimInstance Win32_Process|Where-Object {$_.Name -in @('VideoProcessor.exe','VideoProcessorConfig.exe')}).Count){throw 'An application started during preparation; refusing replacement'}
$replaced=[Collections.Generic.List[object]]::new()
try{
 foreach($f in $files){$replaced.Add($f);New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($f.target)) -Force|Out-Null;if($f.existed){$swapOriginal=Inside $backup ('replaced-originals\'+$f.relative);New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($swapOriginal)) -Force|Out-Null;[IO.File]::Replace($f.staged,$f.target,$swapOriginal);if((Hash $swapOriginal) -ne $f.beforeSha256){throw 'Atomic replacement original hash mismatch'}}else{[IO.File]::Move($f.staged,$f.target)};if((Hash $f.target) -ne $f.expectedSha256){throw "Deployed hash mismatch: $($f.relative)"}}
 foreach($c in $configs){if((Hash (Inside $target $c.file)) -ne $c.sha256){throw 'User settings changed during deployment'}}
 $receipt=[ordered]@{status='deployed-hashes-verified';sourceCommit=$commit;localBase=$plan.localBase;configuration='Release';architecture='x64';timeUtc=[DateTimeOffset]::UtcNow.ToString('o');backupDirectory=$backup;files=$files;retainedDependencies=@($dependencies);nativePassed=$results.Count;configTestPassed=$true;configurationFiles=$configs;configurationEdits=@();runtimeVerification='pending';playerProcessId=$null}
 $receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $out 'deployment-receipt.json') -Encoding utf8
 Copy-Item -LiteralPath (Join-Path $out 'deployment-receipt.json') -Destination (Join-Path $backup 'deployment-receipt.json')
 Write-Output "Verified deployment: $commit. Backup: $backup"
}catch{
 $failure=$_.Exception.Message;$errors=@();foreach($f in $replaced){try{if($f.existed){Copy-Item -LiteralPath $f.backup -Destination $f.target -Force;if((Hash $f.target) -ne $f.beforeSha256){throw 'Rollback hash mismatch'}}else{Remove-Item -LiteralPath $f.target -Force}}catch{$errors+=$_.Exception.Message}}
 [ordered]@{status='deployment-failed';error=$failure;backupDirectory=$backup;rollbackErrors=$errors}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $out 'deployment-failure.json') -Encoding utf8
 throw $failure
}
