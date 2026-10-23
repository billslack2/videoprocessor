$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$vpRoot = 'C:\Videoprocessor\vp'
$vpSource = 'E:\codex\videoprocessor\vp-0205-deploy-20261023'
$vpPin = '24e0dd10430e5140c06c1fa1bd106c9bbde61ef7'
$vpEvidence = Join-Path $vpSource 'artifacts\vp-0205-deploy'
$vpBuild = Get-Content -LiteralPath (Join-Path $vpEvidence 'validated-build.json') -Raw | ConvertFrom-Json
if ($vpBuild.sourceCommit -ne $vpPin -or $vpBuild.configuration -ne 'Release' -or $vpBuild.architecture -ne 'x64' -or $vpBuild.buildExitCode -ne 0 -or $vpBuild.testsFailed -ne 0 -or $vpBuild.testsPassed -lt 1) { throw 'Successful pinned Release build and tests required.' }
if ((& git -C $vpSource rev-parse HEAD) -ne $vpPin -or @(& git -C $vpSource status --porcelain).Count -ne 0) { throw 'Source identity changed.' }
function Get-VpHash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
function Get-VpStreamHash([IO.FileStream]$Stream) {
 $Stream.Position=0; $vpSha=[Security.Cryptography.SHA256]::Create()
 try { [BitConverter]::ToString($vpSha.ComputeHash($Stream)).Replace('-','') } finally { $vpSha.Dispose(); $Stream.Position=0 }
}
$vpSnapshot = Get-Content -LiteralPath (Join-Path $vpEvidence 'predeployment-snapshot.json') -Raw | ConvertFrom-Json
$vpFiles = @($vpBuild.files)
foreach ($vpFile in $vpFiles) { if ((Get-VpHash (Join-Path $vpSource $vpFile.source)) -ne $vpFile.sha256) { throw 'Build payload changed.' } }
$vpGate=$null; $vpHandles=@(); $vpChanged=$false; $vpBackup=$null
try {
 $vpGate=[IO.File]::Open((Join-Path $vpRoot '.vp-update.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::Read)
 $vpApps=@(Get-Process VideoProcessor,VideoProcessor-GUI -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($vpRoot+'\',[StringComparison]::OrdinalIgnoreCase) })
 if ($vpApps.Count) { throw 'VP started during build; close playback before replacing the runtime.' }
 foreach ($vpOld in $vpSnapshot.installedFiles) { if ((Get-VpHash (Join-Path $vpRoot $vpOld.path)) -ne $vpOld.sha256) { throw 'Installed runtime changed during preparation.' } }
 $vpConfigBefore=[ordered]@{}
 $vpConfigFiles=@(Get-ChildItem -LiteralPath $vpRoot -File)+@(Get-ChildItem -LiteralPath (Join-Path $vpRoot 'config') -File)
 foreach ($vpConfig in $vpConfigFiles) { if ($vpConfig.Extension -in @('.cfg','.ini','.state','.json') -and $vpConfig.Name -notlike '*.runtime.json') { $vpConfigBefore[$vpConfig.FullName]=Get-VpHash $vpConfig.FullName } }
 $vpBackup=Join-Path $vpRoot ('backup-vp0205-24e0dd10-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
 New-Item -ItemType Directory -Path $vpBackup | Out-Null
 foreach ($vpFile in $vpFiles) {
  $vpTarget=Join-Path $vpRoot $vpFile.target
  if (-not [IO.Path]::GetFullPath($vpTarget).StartsWith($vpRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Target escaped deployment root.' }
  $vpStream=[IO.File]::Open($vpTarget,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
  $vpHandles+= [pscustomobject]@{ file=$vpFile;target=$vpTarget;stream=$vpStream;oldHash=(Get-VpStreamHash $vpStream);backup=(Join-Path $vpBackup $vpFile.target) }
 }
 foreach ($vpHeld in $vpHandles) {
  New-Item -ItemType Directory -Path (Split-Path -Parent $vpHeld.backup) -Force | Out-Null
  $vpOut=[IO.File]::Open($vpHeld.backup,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try { $vpHeld.stream.Position=0; $vpHeld.stream.CopyTo($vpOut); $vpOut.Flush($true) } finally { $vpOut.Dispose(); $vpHeld.stream.Position=0 }
  if ((Get-VpHash $vpHeld.backup) -ne $vpHeld.oldHash) { throw 'Backup verification failed.' }
 }
 $vpChanged=$true
 foreach ($vpHeld in $vpHandles) {
  $vpIn=[IO.File]::OpenRead((Join-Path $vpSource $vpHeld.file.source))
  try { $vpHeld.stream.Position=0; $vpHeld.stream.SetLength(0); $vpIn.CopyTo($vpHeld.stream); $vpHeld.stream.Flush($true) } finally { $vpIn.Dispose() }
  if ((Get-VpStreamHash $vpHeld.stream) -ne $vpHeld.file.sha256) { throw 'Deployed hash mismatch.' }
 }
 foreach ($vpConfigPath in $vpConfigBefore.Keys) { if ((Get-VpHash $vpConfigPath) -ne $vpConfigBefore[$vpConfigPath]) { throw 'Configuration changed during deployment.' } }
 $vpRecord=[ordered]@{ status='deployed-not-started';sourceCommit=$vpPin;featureCommit='a23c211877685fbe31223c9d9c246ccf3bea008f';retainedInstalledSource=$vpSnapshot.installedSource;branch='codex/vp-0205-deploy-20261023';configuration='Release';architecture='x64';testsPassed=$vpBuild.testsPassed;testsFailed=0;deployedAt=(Get-Date).ToString('o');backup=$vpBackup;configurationEdits=@();configurationHashesUnchanged=$vpConfigBefore;playbackStarted=$false;updaterGateUsed=$true;files=@($vpHandles | ForEach-Object { [ordered]@{source=$_.file.source;target=$_.file.target;beforeSha256=$_.oldHash;sha256=$_.file.sha256;backup=$_.backup} }) }
 $vpRecord | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $vpBackup 'deployment-receipt.json') -Encoding utf8
 $vpRecord | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $vpEvidence 'deployment-receipt.json') -Encoding utf8
} catch {
 if ($vpChanged) {
  foreach ($vpHeld in $vpHandles) {
   $vpRestore=[IO.File]::OpenRead($vpHeld.backup)
   try { $vpHeld.stream.Position=0; $vpHeld.stream.SetLength(0); $vpRestore.CopyTo($vpHeld.stream); $vpHeld.stream.Flush($true) } finally { $vpRestore.Dispose() }
   if ((Get-VpStreamHash $vpHeld.stream) -ne $vpHeld.oldHash) { throw 'Rollback hash mismatch.' }
  }
 }
 throw
} finally {
 foreach ($vpHeld in $vpHandles) { $vpHeld.stream.Dispose() }
 if ($vpGate) { $vpGate.Dispose() }
}
foreach ($vpFile in $vpFiles) { if ((Get-VpHash (Join-Path $vpRoot $vpFile.target)) -ne $vpFile.sha256) { throw 'Post-transaction hash mismatch.' } }
Write-Output ('Deployed verified Release runtime; backup: '+$vpBackup)
Write-Output ('Tests passed: '+$vpBuild.testsPassed+'; configuration unchanged; playback remains stopped.')
