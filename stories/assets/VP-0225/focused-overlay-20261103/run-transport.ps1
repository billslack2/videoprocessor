param([Parameter(Mandatory=$true)][string]$Case,[Parameter(Mandatory=$true)][string]$PluginRoot,[switch]$Visual)
$ErrorActionPreference='Stop'
if(Get-Process VideoProcessor -ErrorAction SilentlyContinue){throw 'Close live VP before isolated GPU tests.'}
$vpRoot=(Get-Location).Path
$vpBase=Join-Path $vpRoot 'artifacts\incident-20261103-181348\visual-pass'
$vpRunner=Join-Path $vpBase 'TestPlatform\vstest.console.exe'
$vpDll=Join-Path $vpRoot 'x64\Release\VideoProcessor-Test.dll'
$vpResults=Join-Path $vpBase ($Case+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $vpResults | Out-Null
$vpMethod='StandardRendererReconstructionTransportOverlayPreservesCurrentUi'
$vpFiles=@($vpDll,$vpRunner)+@(Get-ChildItem -LiteralPath (Join-Path $PluginRoot 'vprenderer') -File -Filter '*.dll' | ForEach-Object FullName)
$vpInventory=@($vpFiles|ForEach-Object{[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash}})
$vpBeforeShots=@(Get-ChildItem -LiteralPath (Join-Path $vpBase 'TestPlatform') -File -Recurse | Where-Object {$_.Directory.Name -eq 'screenshots'} | ForEach-Object FullName)
$vpRecord=[ordered]@{case=$Case;source=(& git rev-parse HEAD).Trim();dirty=@(& git status --porcelain);method=$vpMethod;pluginRoot=$PluginRoot;visual=[bool]$Visual;performanceQualified=$false;gpuIsolated=$true;started=Get-Date;status='running';exit=$null;passed=$false;binaries=$vpInventory;captures=@();results=@();error=$null}
$vpReceipt=Join-Path $vpResults 'receipt.json'
$vpRecord|ConvertTo-Json -Depth 7|Set-Content -LiteralPath $vpReceipt -Encoding UTF8
$vpSavedPlugin=$env:VP_RENDERER_REGRESSION_PLUGIN_ROOT;$vpSavedVisual=$env:VP_TRANSPORT_VISUAL_CAPTURE
$env:VP_RENDERER_REGRESSION_PLUGIN_ROOT=$PluginRoot
$env:VP_TRANSPORT_VISUAL_CAPTURE=if($Visual){'1'}else{$null}
try {
 foreach($vpPath in @($vpDll,$vpResults,$vpRunner)){if($vpPath.Contains('"')){throw 'Unexpected quote in path'}}
 $vpArgs=@(('"'+$vpDll+'"'),'/Platform:x64',('/TestCaseFilter:FullyQualifiedName~'+$vpMethod),'/Logger:trx;LogFileName=transport.trx',('"/ResultsDirectory:'+$vpResults+'"'))
 $vpProcess=Start-Process -FilePath $vpRunner -ArgumentList $vpArgs -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $vpResults 'tests.log') -RedirectStandardError (Join-Path $vpResults 'tests.stderr.log')
 $vpRecord.exit=$vpProcess.ExitCode
 $vpTrx=Join-Path $vpResults 'transport.trx'
 if(-not(Test-Path -LiteralPath $vpTrx)){throw "Runner exited $($vpRecord.exit) without TRX"}
 [xml]$vpXml=Get-Content -LiteralPath $vpTrx -Raw
 $vpRecord.results=@($vpXml.TestRun.Results.UnitTestResult|ForEach-Object{[ordered]@{name=[string]$_.testName;outcome=[string]$_.outcome;error=[string]$_.Output.ErrorInfo.Message;summary=@(([string]$_.Output.StdOut)-split "`r?`n"|Where-Object{$_ -match '^TRANSPORT (UI SUMMARY|VISUAL)'})}})
 $vpRecord.trxHash=(Get-FileHash -LiteralPath $vpTrx).Hash
 $vpRecord.status='completed'
} catch {$vpRecord.status='runner-failed';$vpRecord.error=($_|Out-String)} finally {
 $vpShots=@(Get-ChildItem -LiteralPath (Join-Path $vpBase 'TestPlatform') -File -Recurse | Where-Object {$_.Directory.Name -eq 'screenshots' -and $_.FullName -notin $vpBeforeShots})
 if($vpShots.Count){$vpShotDirectory=Join-Path $vpResults 'captures';New-Item -ItemType Directory -Path $vpShotDirectory|Out-Null
  $vpRecord.captures=@(foreach($vpShot in $vpShots){Copy-Item -LiteralPath $vpShot.FullName -Destination $vpShotDirectory;[ordered]@{name=$vpShot.Name;sha256=(Get-FileHash -LiteralPath $vpShot.FullName).Hash}})}
 $vpRecord.captureCount=@($vpShots|Where-Object Extension -eq '.png').Count
 $vpRecord.capturePairsVerified=$false
 $vpRecord.captureSources=@()
 if($Visual -and $vpRecord.captureCount -eq 5 -and @($vpShots|Where-Object Extension -eq '.json').Count -eq 5) {
  $vpPairValid=$true
  foreach($vpPng in @($vpShots|Where-Object Extension -eq '.png')) {
   $vpJsonPath=[IO.Path]::ChangeExtension($vpPng.FullName,'.json')
   if($vpJsonPath -notin @($vpShots|ForEach-Object FullName)){$vpPairValid=$false;continue}
   try {$vpMetadata=Get-Content -LiteralPath $vpJsonPath -Raw|ConvertFrom-Json
    $vpPairValid=$vpPairValid -and $vpMetadata.schema -eq 'vp-rendered-output-capture-v1' -and $vpMetadata.authoritative_stage -eq 'after-render-before-present' -and $vpMetadata.source_sequence -gt 0 -and $vpMetadata.width -gt 0 -and $vpMetadata.height -gt 0 -and ([string]$vpMetadata.raw_r10_sha256).Length -eq 64
    $vpRecord.captureSources+=@([ordered]@{image=$vpPng.Name;source=$vpMetadata.source_sequence;width=$vpMetadata.width;height=$vpMetadata.height;decision=$vpMetadata.presentation_decision})
   }catch{$vpPairValid=$false}
  }
  $vpRecord.capturePairsVerified=$vpPairValid -and $vpRecord.captureSources.Count -eq 5
 }
 $vpAfter=@($vpFiles|ForEach-Object{[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash}})
 $vpRecord.binariesStable=($vpInventory|ConvertTo-Json -Compress) -eq ($vpAfter|ConvertTo-Json -Compress)
 $vpRecord.sourceStable=(& git rev-parse HEAD).Trim() -eq $vpRecord.source
 $vpRecord.passed=$vpRecord.status -eq 'completed' -and $vpRecord.exit -eq 0 -and $vpRecord.results.Count -eq 1 -and $vpRecord.results[0].name -eq $vpMethod -and $vpRecord.results[0].outcome -eq 'Passed' -and $vpRecord.binariesStable -and $vpRecord.sourceStable -and ((-not $Visual) -or $vpRecord.capturePairsVerified)
 $vpRecord.finished=Get-Date
 $vpRecord|ConvertTo-Json -Depth 7|Set-Content -LiteralPath $vpReceipt -Encoding UTF8
 $env:VP_RENDERER_REGRESSION_PLUGIN_ROOT=$vpSavedPlugin;$env:VP_TRANSPORT_VISUAL_CAPTURE=$vpSavedVisual
}
[pscustomobject]$vpRecord|Select-Object case,status,exit,passed,visual,captureCount,capturePairsVerified,results,error|ConvertTo-Json -Depth 5
Write-Output $vpResults
