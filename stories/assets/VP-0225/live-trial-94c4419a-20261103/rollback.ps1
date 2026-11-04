param([string]$DeploymentReceipt=(Join-Path $PSScriptRoot 'deployment.json'))
$ErrorActionPreference='Stop'
if(Get-Process VideoProcessor -ErrorAction SilentlyContinue){throw 'Close VP before reverting the trial runtime'}
$r=Get-Content -LiteralPath $DeploymentReceipt -Raw|ConvertFrom-Json
if($r.sourceCommit -ne '94c4419ab03443a0ae4d48acdcad43b494da9748' -or @($r.files).Count -ne 28){throw 'Unexpected deployment receipt'}
$destinationRoot='C:\Videoprocessor\vp'
$backupRoot=[IO.Path]::GetFullPath($r.backup).TrimEnd('\')
if(!$backupRoot.StartsWith($destinationRoot+'\backups\trial-94c4419a-',[StringComparison]::OrdinalIgnoreCase)){throw 'Backup is outside this trial'}
foreach($item in $r.files){
 $target=[IO.Path]::GetFullPath((Join-Path $destinationRoot $item.target));$saved=[IO.Path]::GetFullPath((Join-Path $backupRoot $item.target))
 if(!$target.StartsWith($destinationRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or !$saved.StartsWith($backupRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Rollback path escaped expected root'}
 if((Get-FileHash -LiteralPath $target).Hash -ne $item.sha256){throw ('Installed file changed since this trial; refusing overwrite: '+$item.target)}
 if((Get-FileHash -LiteralPath $saved).Hash -ne $item.before){throw ('Backup hash mismatch: '+$item.target)}
}
# Preserve all current user configuration; this trial made no configuration edits.
foreach($item in $r.files){if($item.action -eq 'copy'){Copy-Item -LiteralPath (Join-Path $backupRoot $item.target) -Destination (Join-Path $destinationRoot $item.target) -Force}}
foreach($item in $r.files){if((Get-FileHash -LiteralPath (Join-Path $destinationRoot $item.target)).Hash -ne $item.before){throw ('Rollback hash mismatch: '+$item.target)}}
[ordered]@{status='RuntimeRolledBack';fromCommit=$r.sourceCommit;toCommit='43ba61d4c8cfd24b65003d4c887413bd7969d031';configurationEdits=@();at=(Get-Date).ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $backupRoot 'rollback-receipt.json')
Write-Output 'Verified prior runtime restored; current configuration preserved.'
