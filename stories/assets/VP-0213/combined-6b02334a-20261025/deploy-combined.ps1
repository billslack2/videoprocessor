param([switch]$PreflightOnly,[Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{40}$')][string]$ExpectedCommit)
$ErrorActionPreference='Stop'
$vpSource='E:\codex\videoprocessor\pr177-pr203-deploy-20261025'
$vpInstall='C:\Videoprocessor\vp'
$vpEvidence='E:\codex\diagnostics\pr177-pr203-deploy-20261025'
$vpExpected=$ExpectedCommit
function Assert-VpPath([string]$Path,[string]$Root) {
 $vpAbsolute=[IO.Path]::GetFullPath($Path);$vpBoundary=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
 if(!$vpAbsolute.StartsWith($vpBoundary,[StringComparison]::OrdinalIgnoreCase)){throw "Path outside intended root: $vpAbsolute"}
 return $vpAbsolute
}
if((& git -C $vpSource rev-parse HEAD).Trim() -ne $vpExpected){throw 'Source commit moved'}
if(@(& git -C $vpSource status --porcelain).Count){throw 'Source checkout is not clean'}
$vpValidation=Get-Content -LiteralPath (Join-Path $vpEvidence 'final-validation.json') -Raw | ConvertFrom-Json
if($vpValidation.SourceCommit -ne $vpExpected -or $vpValidation.BuildExitCode -ne 0 -or $vpValidation.NativeFailed -ne 0 -or $vpValidation.ConfigFailed -ne 0){throw 'Final validation gate not satisfied'}
$vpBuild=Join-Path $vpSource 'x64\Release'
foreach($vpHeader in @('src\VideoProcessor-GUI\version.h','src\VideoProcessor-VPRenderer\version.h')){
 $vpIdentity=[IO.File]::ReadAllText((Join-Path $vpSource $vpHeader))
 if(!$vpIdentity.Contains('/commit/'+$vpExpected) -or !$vpIdentity.Contains('VERSION_DIRTY=false')){throw "Build identity is stale or dirty: $vpHeader"}
}
$vpMappings=@(
 @('VideoProcessor-GUI.exe','VideoProcessor.exe'),
 @('VideoProcessorConfig.exe','config\VideoProcessorConfig.exe'),
 @('VideoProcessorConfigDiscovery.dll','config\VideoProcessorConfigDiscovery.dll'),
 @('vprenderer\VideoProcessorVPRenderer.dll','vprenderer\VideoProcessorVPRenderer.dll'),
 @('vprenderer-beta\VideoProcessorVPRendererBeta.dll','vprenderer-beta\VideoProcessorVPRendererBeta.dll')
)
$vpFiles=@()
foreach($vpMapping in $vpMappings){
 $vpBinary=Assert-VpPath (Join-Path $vpBuild $vpMapping[0]) $vpSource
 $vpRecord=Get-Content -LiteralPath ($vpBinary+'.runtime.json') -Raw | ConvertFrom-Json
 if($vpRecord.configuration -ne 'Release' -or $vpRecord.architecture -ne 'x64' -or (Get-FileHash -LiteralPath $vpBinary).Hash -ne $vpRecord.sha256){throw "Release record mismatch: $vpBinary"}
 $vpFiles += [pscustomobject]@{Source=$vpBinary;Relative=$vpMapping[1]}
 $vpFiles += [pscustomobject]@{Source=$vpBinary+'.runtime.json';Relative=$vpMapping[1]+'.runtime.json'}
 $vpSourcePdb=[IO.Path]::ChangeExtension($vpBinary,'.pdb')
 if(Test-Path -LiteralPath $vpSourcePdb){$vpFiles += [pscustomobject]@{Source=$vpSourcePdb;Relative=$(if($vpMapping[1] -eq 'VideoProcessor.exe'){'VideoProcessor-GUI.pdb'}else{[IO.Path]::ChangeExtension($vpMapping[1],'.pdb')})}}
}
# Private runtime dependencies must match this build; none are settings or caches.
foreach($vpFolder in @('vprenderer','vprenderer-beta')){
 foreach($vpDependency in Get-ChildItem -LiteralPath (Join-Path $vpBuild $vpFolder) -File -Filter '*.dll'){
  if($vpDependency.Name -like 'VideoProcessor*'){continue}
  $vpTarget=Assert-VpPath (Join-Path $vpInstall ($vpFolder+'\'+$vpDependency.Name)) $vpInstall
  if(!(Test-Path -LiteralPath $vpTarget) -or (Get-FileHash -LiteralPath $vpDependency.FullName).Hash -ne (Get-FileHash -LiteralPath $vpTarget).Hash){
   $vpFiles += [pscustomobject]@{Source=$vpDependency.FullName;Relative=$vpFolder+'\'+$vpDependency.Name}
  }
 }
}
$vpManifest=Get-Content -LiteralPath (Join-Path $vpSource 'packaging\release-manifest.json') -Raw | ConvertFrom-Json
foreach($vpEntry in $vpManifest.files){
 if($vpEntry.sourceRoot -ne 'build' -or $vpEntry.destination -notlike 'config/*.dll' -or $vpEntry.source -eq 'VideoProcessorConfigDiscovery.dll'){continue}
 $vpDependency=Assert-VpPath (Join-Path $vpBuild $vpEntry.source) $vpBuild
 if(!(Test-Path -LiteralPath $vpDependency)){throw "Missing Config dependency: $vpDependency"}
 $vpTarget=Assert-VpPath (Join-Path $vpInstall $vpEntry.destination) $vpInstall
 if(!(Test-Path -LiteralPath $vpTarget) -or (Get-FileHash -LiteralPath $vpDependency).Hash -ne (Get-FileHash -LiteralPath $vpTarget).Hash){
  $vpFiles += [pscustomobject]@{Source=$vpDependency;Relative=$vpEntry.destination}
 }
}
$vpConfigFiles=@(Get-ChildItem -LiteralPath $vpInstall,(Join-Path $vpInstall 'config') -File | Where-Object {$_.Extension -in @('.cfg','.ini','.state','.json','.vpprofile') -and $_.Name -notlike '*.runtime.json'})
$vpConfigBefore=@($vpConfigFiles | ForEach-Object {[pscustomobject]@{Path=$_.FullName;SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
if($PreflightOnly){
 $vpFiles | Select-Object Source,Relative | ConvertTo-Json
 "Preflight complete: $($vpFiles.Count) deployable files; $($vpConfigBefore.Count) configuration/state files will be preserved. No installation changes."
 return
}
$vpProcesses=@(Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -and $_.ExecutablePath.StartsWith($vpInstall+'\',[StringComparison]::OrdinalIgnoreCase) -and $_.Name -in @('VideoProcessor.exe','VideoProcessorConfig.exe')})
if($vpProcesses.Count){throw 'VP or Config is still running; preserve pending edits and exit it before deployment'}
$vpBackup=Assert-VpPath (Join-Path $vpInstall ('backup-before-pr177-pr203-'+$vpExpected.Substring(0,8)+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))) $vpInstall
if(Test-Path -LiteralPath $vpBackup){throw 'Choose a new backup directory'}
New-Item -ItemType Directory -Path $vpBackup | Out-Null
$vpPrepared=@()
foreach($vpFile in $vpFiles){
 $vpTarget=Assert-VpPath (Join-Path $vpInstall $vpFile.Relative) $vpInstall
 $vpOld=Assert-VpPath (Join-Path $vpBackup $vpFile.Relative) $vpBackup
 $vpStage=Assert-VpPath (Join-Path $vpBackup ('staged\'+$vpFile.Relative)) $vpBackup
 $vpAtomic=Assert-VpPath (Join-Path $vpBackup ('atomic\'+$vpFile.Relative)) $vpBackup
 foreach($vpDirectory in @((Split-Path -Parent $vpOld),(Split-Path -Parent $vpStage),(Split-Path -Parent $vpAtomic),(Split-Path -Parent $vpTarget))){New-Item -ItemType Directory -Path $vpDirectory -Force | Out-Null}
 $vpExists=Test-Path -LiteralPath $vpTarget
 $vpOldHash=$null
 if($vpExists){$vpOldHash=(Get-FileHash -LiteralPath $vpTarget).Hash;Copy-Item -LiteralPath $vpTarget -Destination $vpOld;if((Get-FileHash -LiteralPath $vpOld).Hash -ne $vpOldHash){throw 'Backup hash mismatch'}}
 Copy-Item -LiteralPath $vpFile.Source -Destination $vpStage
 $vpNewHash=(Get-FileHash -LiteralPath $vpFile.Source).Hash
 if((Get-FileHash -LiteralPath $vpStage).Hash -ne $vpNewHash){throw 'Staged hash mismatch'}
 $vpPrepared += [pscustomobject]@{Source=$vpFile.Source;Target=$vpTarget;Relative=$vpFile.Relative;Backup=$vpOld;Stage=$vpStage;Atomic=$vpAtomic;Existed=$vpExists;OldHash=$vpOldHash;NewHash=$vpNewHash}
}
foreach($vpConfig in $vpConfigBefore){
 $vpRelative=$vpConfig.Path.Substring($vpInstall.Length+1)
 $vpCopy=Assert-VpPath (Join-Path $vpBackup ('user-config\'+$vpRelative)) $vpBackup
 New-Item -ItemType Directory -Path (Split-Path -Parent $vpCopy) -Force | Out-Null
 Copy-Item -LiteralPath $vpConfig.Path -Destination $vpCopy
 if((Get-FileHash -LiteralPath $vpCopy).Hash -ne $vpConfig.SHA256){throw 'Configuration changed during backup; stop and inspect'}
}
$vpCompleted=@()
try {
 foreach($vpFile in $vpPrepared){
  if($vpFile.Existed){[IO.File]::Replace($vpFile.Stage,$vpFile.Target,$vpFile.Atomic)}
  else{Move-Item -LiteralPath $vpFile.Stage -Destination $vpFile.Target}
  $vpCompleted += $vpFile
 }
 foreach($vpFile in $vpPrepared){if((Get-FileHash -LiteralPath $vpFile.Target).Hash -ne $vpFile.NewHash){throw 'Deployed hash mismatch'}}
 foreach($vpConfig in $vpConfigBefore){if((Get-FileHash -LiteralPath $vpConfig.Path).Hash -ne $vpConfig.SHA256){throw 'User configuration changed during deployment'}}
} catch {
 foreach($vpFile in $vpCompleted){
  if($vpFile.Existed){Copy-Item -LiteralPath $vpFile.Backup -Destination $vpFile.Target -Force}
  else{Remove-Item -LiteralPath $vpFile.Target}
 }
 foreach($vpFile in $vpCompleted){if($vpFile.Existed -and (Get-FileHash -LiteralPath $vpFile.Target).Hash -ne $vpFile.OldHash){throw 'Rollback verification failed'}}
 throw
}
$vpReceipt=[pscustomobject]@{SourceCommit=$vpExpected;BetaCommit='8c4537462bbabd6ce07ba3e9ffd9be9bc8745492';BackupDirectory=$vpBackup;ConfigurationEdits=@();ConfigurationUnchanged=$true;ConfigurationBefore=$vpConfigBefore;Files=$vpPrepared;DeployedAt=(Get-Date).ToString('o');PlayerStarted=$false;ConfigurationEditorStarted=$false}
$vpReceipt | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $vpEvidence 'deployment-receipt.json')
$vpReceipt | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $vpBackup 'deployment-receipt.json')
"Deployed $($vpPrepared.Count) verified files; configuration unchanged. Backup: $vpBackup"

