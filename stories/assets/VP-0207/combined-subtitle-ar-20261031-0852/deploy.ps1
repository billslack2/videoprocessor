param([switch]$Apply)
$ErrorActionPreference='Stop'
$source='E:\codex\videoprocessor\combined-subtitle-ar-deploy-20261031'
$build=Join-Path $source 'x64\Release'
$evidence='E:\codex\diagnostics\combined-subtitle-ar-deploy-20261031-0847'
$destination='C:\Videoprocessor\vp'
$commit='27dc733ad6b7f5afa01a13c1a114ff9337cc40a8'
if ((& git -C $source rev-parse HEAD).Trim() -ne $commit -or @(& git -C $source status --porcelain).Count) { throw 'Source identity changed' }
if ((Get-Content (Join-Path $evidence 'release-build-passed.txt') -Raw).Trim() -ne $commit) { throw 'Completed Release build required' }
$previous=@{
 'VideoProcessor.exe'='C7419B92E3C52CA18D3364A2C1AC002F8B352503801C91F5179FC29FE5EBD0EB'
 'vprenderer\VideoProcessorVPRenderer.dll'='E884A2BD8DEBB29D8EFB5B4E74F8CE4C5592A36695E207BD816B11967C728C90'
 'vprenderer-beta\VideoProcessorVPRendererBeta.dll'='338F893B756009FD9033F548343059997B1B4628DDD341912DDDED0727E42503'
}
foreach ($name in $previous.Keys) { if ((Get-FileHash (Join-Path $destination $name)).Hash -ne $previous[$name]) { throw "Installed runtime changed: $name" } }
$files=@(
 @{source='VideoProcessor-GUI.exe';target='VideoProcessor.exe'},
 @{source='vprenderer\VideoProcessorVPRenderer.dll';target='vprenderer\VideoProcessorVPRenderer.dll'},
 @{source='vprenderer-beta\VideoProcessorVPRendererBeta.dll';target='vprenderer-beta\VideoProcessorVPRendererBeta.dll'}
)
foreach ($file in @($files)) {
 $path=Join-Path $build $file.source
 $record=Get-Content ($path+'.runtime.json') -Raw | ConvertFrom-Json
 $bytes=[IO.File]::ReadAllBytes($path)
 $pe=[BitConverter]::ToInt32($bytes,0x3c)
 if ([BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x8664) { throw "Not x64: $path" }
 if ($record.configuration -ne 'Release' -or $record.architecture -ne 'x64' -or $record.sha256 -ne (Get-FileHash $path).Hash) { throw "Invalid Release record: $path" }
 if (![Text.Encoding]::Unicode.GetString($bytes).Contains('https://github.com/billslack2/videoprocessor/commit/'+$commit)) { throw "Wrong embedded commit: $path" }
 $files+=@{source=$file.source+'.runtime.json';target=$file.target+'.runtime.json'}
}
foreach ($folder in @('vprenderer','vprenderer-beta')) {
 foreach ($dll in Get-ChildItem (Join-Path $build $folder) -Filter '*.dll') {
  $relative=Join-Path $folder $dll.Name
  if ($files.target -contains $relative) { continue }
  $installed=Join-Path $destination $relative
  if (!(Test-Path $installed) -or (Get-FileHash $dll.FullName).Hash -ne (Get-FileHash $installed).Hash) { $files+=@{source=$relative;target=$relative} }
 }
}
foreach ($pdb in @('VideoProcessor-GUI.pdb','vprenderer\VideoProcessorVPRenderer.pdb','vprenderer-beta\VideoProcessorVPRendererBeta.pdb')) { $files+=@{source=$pdb;target=$pdb} }
$files+=@{source='VideoProcessor-GUI.exe.runtime.json';target='VideoProcessor-GUI.exe.runtime.json'}
foreach ($file in $files) { $file.sha256=(Get-FileHash (Join-Path $build $file.source)).Hash }
if (!$Apply) { [pscustomobject]@{commit=$commit;configuration='x64 Release';tests='Skipped at user request';files=$files} | ConvertTo-Json -Depth 5; return }
$backup=Join-Path $destination ('backups\combined-subtitle-ar-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup | Out-Null
$configHashes=@{}
foreach ($name in @('VideoProcessor.cfg','VideoProcessor.state')) {
 $path=Join-Path $destination $name
 $configHashes[$name]=(Get-FileHash $path).Hash
 Copy-Item -LiteralPath $path -Destination (Join-Path $backup $name)
}
foreach ($file in $files) {
 $old=Join-Path $destination $file.target
 $saved=Join-Path $backup $file.target
 $file.before=$null
 if (Test-Path $old) {
  $file.before=(Get-FileHash $old).Hash
  New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null
  Copy-Item -LiteralPath $old -Destination $saved
  if ((Get-FileHash $saved).Hash -ne $file.before) { throw 'Backup verification failed' }
 }
}
$players=@(Get-Process -Name VideoProcessor -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $destination 'VideoProcessor.exe') })
$stopped=@()
foreach ($player in $players) {
 $stopped+=$player.Id
 if (!$player.CloseMainWindow()) { throw "Graceful close request failed: $($player.Id)" }
 if (!$player.WaitForExit(30000)) { throw "Player did not close: $($player.Id)" }
}
foreach ($name in @('VideoProcessor.cfg','VideoProcessor.state')) {
 $path=Join-Path $destination $name
 $afterClose=(Get-FileHash $path).Hash
 if ($afterClose -ne $configHashes[$name]) {
  Copy-Item -LiteralPath $path -Destination (Join-Path $backup ($name+'.after-close'))
  $configHashes[$name]=$afterClose
 }
}
try {
 foreach ($file in $files) {
  $target=Join-Path $destination $file.target
  New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
  Copy-Item -LiteralPath (Join-Path $build $file.source) -Destination $target -Force
 }
 foreach ($file in $files) { if ((Get-FileHash (Join-Path $destination $file.target)).Hash -ne $file.sha256) { throw 'Installed hash mismatch' } }
 foreach ($name in $configHashes.Keys) { if ((Get-FileHash (Join-Path $destination $name)).Hash -ne $configHashes[$name]) { throw "Configuration changed: $name" } }
} catch {
 foreach ($file in $files) {
  $target=[IO.Path]::GetFullPath((Join-Path $destination $file.target))
  if (!$target.StartsWith(($destination+'\'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Rollback target escapes deployment root' }
  if ($file.before) { Copy-Item -LiteralPath (Join-Path $backup $file.target) -Destination $target -Force }
  elseif (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
 }
 throw
}
$log=Join-Path $destination 'logs\vp.log'
$logOffset=if(Test-Path $log){(Get-Item $log).Length}else{0}
$started=Start-Process -FilePath (Join-Path $destination 'VideoProcessor.exe') -WorkingDirectory $destination -PassThru
$receipt=[ordered]@{commit=$commit;baseCommit='a591ad3b532771911e85f8ef026958279f2065f0';branch='codex/combined-subtitle-ar-deploy-20261031';configuration='Release';architecture='x64';testsRun=$false;testsSkippedReason='User explicitly requested skip the tests';backup=$backup;configurationEdits=@();configurationHashes=$configHashes;files=$files;stoppedProcessIds=$stopped;processId=$started.Id;startedAt=(Get-Date).ToString('o');logOffsetBeforeStart=$logOffset}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $evidence 'deployment-receipt.json') -Encoding utf8
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backup 'deployment-receipt.json') -Encoding utf8
[pscustomobject]@{commit=$commit;backup=$backup;processId=$started.Id;fileCount=$files.Count;testsRun=$false;configurationEdits=@()} | ConvertTo-Json
