[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$vpExpectedCommit='05e59c91e40bcde9f75167506044b7671aca8503'
$vpSource='E:\codex\videoprocessor\deploy-startup-subtitle-20261022'
$vpInstall='C:\Videoprocessor\vp'
$vpWork='C:\Users\bslac\Documents\Codex\2026-10-22\befo\work'
if ((git -C $vpSource rev-parse HEAD) -ne $vpExpectedCommit) {throw 'Deployment source moved'}
if (git -C $vpSource status --porcelain) {throw 'Deployment source is dirty'}
foreach($vpHeader in @('src\VideoProcessor-GUI\version.h','src\VideoProcessor-VPRenderer\version.h')) {
 $vpVersion=[IO.File]::ReadAllText((Join-Path $vpSource $vpHeader))
 if($vpVersion -notmatch [regex]::Escape($vpExpectedCommit) -or $vpVersion -notmatch 'VERSION_DIRTY\s*=\s*false') {throw "Build identity is not clean/current: $vpHeader"}
}
$vpBuildLog=Join-Path $vpWork 'vp-deploy-build.log'
if([IO.File]::ReadAllText($vpBuildLog) -notmatch '0 Error\(s\)') {throw 'Successful Release build evidence missing'}
[xml]$vpTrx=Get-Content -LiteralPath (Join-Path $vpWork 'deploy-policy-tests\vp-deploy-policy.trx') -Raw
$vpNs=New-Object Xml.XmlNamespaceManager($vpTrx.NameTable)
$vpNs.AddNamespace('t','http://microsoft.com/schemas/VisualStudio/TeamTest/2010')
$vpCounters=$vpTrx.SelectSingleNode('//t:ResultSummary/t:Counters',$vpNs)
if(!$vpCounters -or [int]$vpCounters.failed -ne 0 -or [int]$vpCounters.total -le 1299 -or [int]$vpCounters.passed -ne [int]$vpCounters.total) {throw 'Combined-source regression suite did not pass'}
$vpEntries=@(
 [pscustomobject]@{Source='VideoProcessor-GUI.exe';Destination='VideoProcessor.exe';OldHash='640736B773E05AFA4A26DF5860EC447195D689B1857D486E271E37DAAE26511B'},
 [pscustomobject]@{Source='vprenderer\VideoProcessorVPRenderer.dll';Destination='vprenderer\VideoProcessorVPRenderer.dll';OldHash='1591AD8ABD425C1065818F3439B87B82C7ADB76E1D0B7CDB1597B8AD6CF58793'},
 [pscustomobject]@{Source='vprenderer-beta\VideoProcessorVPRendererBeta.dll';Destination='vprenderer-beta\VideoProcessorVPRendererBeta.dll';OldHash='9D0C0834A29765D85B08EB7B963CF4B99B47C65FE84DD4D8AFC80A6C978434C4'}
)
function Get-VpHash([string]$vpFile) { (Get-FileHash -LiteralPath $vpFile -Algorithm SHA256).Hash }
function Assert-VpInstallPath([string]$vpFile) {
 $vpResolved=[IO.Path]::GetFullPath($vpFile)
 if(!$vpResolved.StartsWith($vpInstall+'\',[StringComparison]::OrdinalIgnoreCase)) {throw "Path outside deployment: $vpResolved"}
}
function Assert-VpX64([string]$vpFile) {
 $vpStream=[IO.File]::OpenRead($vpFile)
 try {
  $vpReader=New-Object IO.BinaryReader($vpStream)
  if($vpReader.ReadUInt16() -ne 0x5a4d) {throw 'Missing MZ signature'}
  $vpStream.Position=0x3c;$vpPe=$vpReader.ReadInt32();$vpStream.Position=$vpPe
  if($vpReader.ReadUInt32() -ne 0x4550 -or $vpReader.ReadUInt16() -ne 0x8664) {throw "Not a Windows x64 PE: $vpFile"}
 } finally {$vpStream.Dispose()}
}
$vpRelease=Join-Path $vpSource 'x64\Release'
foreach($vpEntry in $vpEntries) {
 $vpFrom=Join-Path $vpRelease $vpEntry.Source;$vpTo=Join-Path $vpInstall $vpEntry.Destination
 Assert-VpInstallPath $vpTo;Assert-VpX64 $vpFrom
 $vpRuntime=Get-Content -LiteralPath ($vpFrom+'.runtime.json') -Raw | ConvertFrom-Json
 $vpHash=Get-VpHash $vpFrom
 if($vpRuntime.configuration -ne 'Release' -or $vpRuntime.architecture -ne 'x64' -or $vpRuntime.sha256 -ne $vpHash) {throw "Runtime hash record mismatch: $vpFrom"}
 if((Get-VpHash $vpTo) -ne $vpEntry.OldHash) {throw "Installed runtime changed during preparation: $vpTo"}
 $vpEntry | Add-Member -NotePropertyName NewHash -NotePropertyValue $vpHash
}
# Deployment never edits user configuration or state. Hash them immediately
# before and after replacement so the receipt records their exact disposition.
$vpConfigFiles=@(Get-ChildItem -LiteralPath $vpInstall -File | Where-Object {$_.Extension -in @('.cfg','.ini','.state')})
$vpConfigBefore=@{};foreach($vpConfig in $vpConfigFiles) {$vpConfigBefore[$vpConfig.FullName]=Get-VpHash $vpConfig.FullName}
$vpPlayers=@(Get-CimInstance Win32_Process -Filter "Name='VideoProcessor.exe'" | Where-Object {$_.ExecutablePath -eq (Join-Path $vpInstall 'VideoProcessor.exe')})
foreach($vpPlayer in $vpPlayers) {
 $vpProcess=Get-Process -Id $vpPlayer.ProcessId
 if(!$vpProcess.CloseMainWindow() -or !$vpProcess.WaitForExit(30000)) {throw 'VP did not close normally; binaries were not replaced'}
}
$vpBackup=Join-Path $vpInstall ('backup-before-vp0201-'+$vpExpectedCommit.Substring(0,8)+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
Assert-VpInstallPath $vpBackup
if(Test-Path -LiteralPath $vpBackup) {throw 'Backup directory already exists'}
New-Item -ItemType Directory -Path $vpBackup | Out-Null
foreach($vpEntry in $vpEntries) {
 $vpBackupFile=Join-Path $vpBackup $vpEntry.Destination;Assert-VpInstallPath $vpBackupFile
 New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($vpBackupFile)) -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $vpInstall $vpEntry.Destination) -Destination $vpBackupFile
 if((Get-VpHash $vpBackupFile) -ne $vpEntry.OldHash) {throw "Backup hash mismatch: $vpBackupFile"}
}
$vpStaged=@()
try {
 foreach($vpEntry in $vpEntries) {
  $vpTarget=Join-Path $vpInstall $vpEntry.Destination
  $vpTemporary=$vpTarget+'.codex-'+[guid]::NewGuid().ToString('N')+'.tmp';Assert-VpInstallPath $vpTemporary
  Copy-Item -LiteralPath (Join-Path $vpRelease $vpEntry.Source) -Destination $vpTemporary
  if((Get-VpHash $vpTemporary) -ne $vpEntry.NewHash) {throw 'Staged runtime hash mismatch'}
  $vpStaged+= [pscustomobject]@{Temporary=$vpTemporary;Target=$vpTarget}
 }
 foreach($vpStage in $vpStaged) {
  $vpAtomicBackup=Join-Path $vpBackup ('atomic-'+[IO.Path]::GetFileName($vpStage.Target));Assert-VpInstallPath $vpAtomicBackup
  [IO.File]::Replace($vpStage.Temporary,$vpStage.Target,$vpAtomicBackup)
 }
 foreach($vpEntry in $vpEntries) { if((Get-VpHash (Join-Path $vpInstall $vpEntry.Destination)) -ne $vpEntry.NewHash) {throw 'Deployed runtime hash mismatch'} }
} catch {
 $vpDeploymentError=$_
 foreach($vpEntry in $vpEntries) {Copy-Item -LiteralPath (Join-Path $vpBackup $vpEntry.Destination) -Destination (Join-Path $vpInstall $vpEntry.Destination) -Force}
 throw $vpDeploymentError
} finally {
 foreach($vpStage in $vpStaged) {if(Test-Path -LiteralPath $vpStage.Temporary) {Assert-VpInstallPath $vpStage.Temporary;Remove-Item -LiteralPath $vpStage.Temporary}}
}
$vpConfigAfter=@{};foreach($vpConfig in $vpConfigFiles) {$vpConfigAfter[$vpConfig.FullName]=Get-VpHash $vpConfig.FullName}
$vpConfigUnchanged=$true;foreach($vpConfig in $vpConfigFiles) {if($vpConfigBefore[$vpConfig.FullName] -ne $vpConfigAfter[$vpConfig.FullName]) {$vpConfigUnchanged=$false}}
$vpReceipt=[ordered]@{
 sourceCommit=$vpExpectedCommit;sourceBranch='codex/deploy-startup-subtitle-20261022';sourceTree=(git -C $vpSource rev-parse 'HEAD^{tree}')
 featureCommit='6f12ff913b51fbedbf9cc0c3a6610bb45a588a5b';preservedInstalledCommit='620b7d95e8decb64ab4472ebd302193ce8c91ce8'
 buildConfiguration='Release';architecture='x64';backupDirectory=$vpBackup;files=$vpEntries
 configurationEdits=@();configurationBefore=$vpConfigBefore;configurationAfter=$vpConfigAfter;configurationUnchanged=$vpConfigUnchanged
 testsPassed=[int]$vpCounters.passed;testResult=(Join-Path $vpWork 'deploy-policy-tests\vp-deploy-policy.trx')
 playerWasRunning=($vpPlayers.Count -gt 0);playerStarted=$false;timestamp=(Get-Date).ToString('o')
}
$vpReceipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $vpBackup 'deployment-receipt.json') -Encoding UTF8
$vpReceipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $vpWork 'vp-deployment-receipt.json') -Encoding UTF8
$vpReceipt | ConvertTo-Json -Depth 8
