$ErrorActionPreference='Stop'
$vpValidation='E:\codex\videoprocessor\ar-evidence-continuity-20261031\x64\Release\ar-validation'
$vpReceipt=Get-Content (Join-Path $vpValidation 'deployment-receipt.json') -Raw | ConvertFrom-Json
$vpRoot='C:\Videoprocessor\vp'
$vpLog=Join-Path $vpRoot 'logs\vp.log'
$vpProcess=@(Get-Process VideoProcessor | Where-Object Path -eq (Join-Path $vpRoot 'VideoProcessor.exe'))
if($vpProcess.Count -ne 1 -or !$vpProcess[0].Responding){throw 'Expected one responding deployed player'}
$vpStart=$vpProcess[0].StartTime
if((Get-Item $vpLog).LastWriteTime -lt $vpStart){throw 'Runtime log predates restarted player'}
$vpHeader=Get-Content $vpLog -TotalCount 300
$vpIdentity=@($vpHeader | Select-String 'VP build identity:' | ForEach-Object Line)
foreach($vpModule in @('host','renderer')){
    if(!($vpIdentity | Where-Object {$_ -match ('module='+$vpModule+' ') -and $_.Contains('/commit/'+$vpReceipt.commit) -and $_.Contains('dirty=0')})){throw "Runtime identity failed: $vpModule"}
}
$vpModules=@($vpProcess[0].Modules | Where-Object ModuleName -Match 'VideoProcessorVPRenderer|libplacebo' | ForEach-Object FileName)
if($vpModules -notcontains (Join-Path $vpRoot 'vprenderer\VideoProcessorVPRenderer.dll')){throw 'Expected installed standard renderer is not loaded'}
function Get-VpOutputSequence {
    $line=Get-Content $vpLog -Tail 300 | Select-String 'VP output correlation: generation=(\d+) source=(\d+)' | Select-Object -Last 1
    if(!$line){throw 'No current output correlation'}
    [pscustomobject]@{generation=[long]$line.Matches[0].Groups[1].Value;sequence=[long]$line.Matches[0].Groups[2].Value;line=$line.Line}
}
$vpFirst=Get-VpOutputSequence
Start-Sleep -Seconds 10
$vpLast=Get-VpOutputSequence
if($vpFirst.generation -ne $vpLast.generation -or $vpLast.sequence -le $vpFirst.sequence){throw 'Output source sequence did not advance within one generation'}
foreach($vpFile in $vpReceipt.files){if((Get-FileHash (Join-Path $vpRoot $vpFile.path)).Hash -ne $vpFile.after){throw 'Installed artifact changed after deployment'}}
foreach($vpFile in $vpReceipt.configurationHashes.PSObject.Properties){if((Get-FileHash (Join-Path $vpRoot $vpFile.Name)).Hash -ne $vpFile.Value){throw "Configuration changed at startup: $($vpFile.Name)"}}
$vpResult=[pscustomobject]@{commit=$vpReceipt.commit;processId=$vpProcess[0].Id;started=$vpStart.ToString('o');responding=$true;runtimeIdentities=$vpIdentity;loadedModules=$vpModules;firstOutput=$vpFirst;lastOutput=$vpLast;installedHashesVerified=$true;configurationUnchanged=$true;physicalPlaybackAcceptance='Pending user visual review'}
$vpResult | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $vpValidation 'runtime-verification.json')
$vpResult | ConvertTo-Json -Depth 5
