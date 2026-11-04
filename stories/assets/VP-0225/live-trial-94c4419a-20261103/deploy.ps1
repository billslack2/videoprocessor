param([string]$NativeEvidence,[string]$PrivateEvidence,[string]$BetaEvidence,[string]$StandardEvidence,[string]$BetaBaselineEvidence)
$ErrorActionPreference='Stop'
$root='E:\codex\videoprocessor\subtitle-pixel-tolerance-20261103'
$deployRoot='C:\Videoprocessor\vp'
$buildRoot=Join-Path $root 'x64\Release'
$pin='94c4419ab03443a0ae4d48acdcad43b494da9748'
if(!$StandardEvidence){$StandardEvidence=Join-Path $root 'artifacts\qualification\runs\20261103-184142-b4d3a043\iteration.json'}
$focusedPath=[IO.Path]::GetFullPath($StandardEvidence)
if(!$BetaEvidence -or !$BetaBaselineEvidence){throw 'Provide both candidate and preserved43 Beta startup failure receipts for this explicitly limited Standard-renderer trial'}
$betaPath=[IO.Path]::GetFullPath($BetaEvidence)
$betaBaselinePath=[IO.Path]::GetFullPath($BetaBaselineEvidence)
# Discovery accepts only completed evidence for this exact pin. An absent
# pending run is not approval to proceed.
if(!$NativeEvidence) {
 $NativeEvidence=Get-ChildItem -LiteralPath (Join-Path $root 'artifacts\qualification\runs') -Directory | Sort-Object Name -Descending | ForEach-Object {
  $p=Join-Path $_.FullName 'iteration.json'
  if(Test-Path -LiteralPath $p){$r=Get-Content -LiteralPath $p -Raw|ConvertFrom-Json;if($r.status -eq 'NativePassed' -and $r.source.commit -eq $pin -and !$r.source.dirty){$p}}
 } | Select-Object -First 1
}
if(!$NativeEvidence){throw 'No completed NativePassed evidence for 94c4419a; deployment has not started'}
$nativePath=[IO.Path]::GetFullPath($NativeEvidence)
if(!$PrivateEvidence) {
 $PrivateEvidence=Get-ChildItem -LiteralPath (Join-Path $root 'artifacts\subtitle-tolerance\real-renderer') -Directory | Sort-Object Name -Descending | ForEach-Object {
  $p=Join-Path $_.FullName 'receipt.json'
  if(Test-Path -LiteralPath $p){$r=Get-Content -LiteralPath $p -Raw|ConvertFrom-Json;if($r.status -eq 'completed' -and $r.passed -and $r.testSource -eq $pin -and !$r.dirty){$p}}
 } | Select-Object -First 1
}
if(!$PrivateEvidence){throw 'No completed actual-capture GPU pass for 94c4419a; deployment has not started'}
$privatePath=[IO.Path]::GetFullPath($PrivateEvidence)
. (Join-Path $root 'tools\installer_build_identity.ps1')
$identity=Get-VpSourceIdentity $root
if($identity.commit -ne $pin -or $identity.dirty){throw 'Source is not the pinned clean candidate'}
$focused=Get-Content -LiteralPath $focusedPath -Raw|ConvertFrom-Json
$native=Get-Content -LiteralPath $nativePath -Raw|ConvertFrom-Json
$beta=Get-Content -LiteralPath $betaPath -Raw|ConvertFrom-Json
. (Join-Path $root 'tools\runtime_qualification.ps1')
$criticalPlan=Get-VpCriticalTestPlan $root
$nativeExpected=$criticalPlan.nativeCount
foreach($r in @($focused,$native)) {
 if($r.source.commit -ne $pin -or $r.source.dirty -or $r.source.fingerprint -ne $identity.fingerprint -or @($r.builds|Where-Object exit -NE 0).Count -or @($r.runs|Where-Object testExit -NE 0).Count){throw 'Build/test source or result mismatch'}
 foreach($run in $r.runs){if((Get-FileHash -LiteralPath (Join-Path $root $run.trx)).Hash -ne $run.trxSha256){throw 'Test evidence hash mismatch'}}
}
if($focused.status -ne 'FocusedPassed' -or $native.status -ne 'NativePassed' -or ($native.runs|Measure-Object -Property executed -Sum).Sum -ne $nativeExpected){throw 'Required focused/native evidence missing'}
foreach($r in @($focused)) {
 foreach($item in $r.binaries){if((Get-FileHash -LiteralPath (Join-Path $root $item.path)).Hash -ne $item.sha256){throw ('Build output changed: '+$item.path)}}
}
$overlayMethods=@('StandardRendererReconstructionTransportOverlayPreservesCurrentUi','BetaRendererReconstructionTransportOverlayPreservesCurrentUi')
for($i=0;$i -lt 1;$i++) {
 $r=$focused
 if(@($r.runs).Count -ne 1 -or $r.runs[0].executed -ne 1){throw 'Focused overlay evidence must contain exactly the requested test'}
 [xml]$overlayXml=Get-Content -LiteralPath (Join-Path $root $r.runs[0].trx) -Raw
 $overlayCases=@($overlayXml.TestRun.Results.UnitTestResult)
 if($overlayCases.Count -ne 1 -or $overlayCases[0].testName -ne $overlayMethods[$i] -or $overlayCases[0].outcome -ne 'Passed' -or [string]$overlayCases[0].Output.StdOut -notmatch 'TRANSPORT UI SUMMARY:' -or [string]$overlayCases[0].Output.StdOut -match 'TRANSPORT VISUAL ONLY:'){throw 'Expected passing non-visual actual overlay method is missing'}
}
# The candidate Beta case and the preserved43 Beta case both stop at the same
# initial-readiness assertion before exercising transport UI. This is recorded
# as a known failure, never converted into a passing Beta overlay qualification.
$betaBaseline=Get-Content -LiteralPath $betaBaselinePath -Raw|ConvertFrom-Json
$betaMethod='BetaRendererReconstructionTransportOverlayPreservesCurrentUi'
$betaFailure='Assert failed. Ready UHD output lost its current caption or changed the native viewport.'
if($beta.status -ne 'Failed' -or $beta.source.commit -ne $pin -or $beta.source.dirty -or $beta.source.fingerprint -ne $identity.fingerprint -or
 @($beta.builds|Where-Object exit -ne 0).Count -or @($beta.runs).Count -ne 1 -or $beta.runs[0].testExit -ne 1){throw 'Candidate Beta failure is not the matched clean-source startup failure'}
$betaTrx=Join-Path $root $beta.runs[0].trx
if((Get-FileHash -LiteralPath $betaTrx).Hash -ne $beta.runs[0].trxSha256){throw 'Candidate Beta failure TRX hash mismatch'}
$baselineRoot=Join-Path $root 'artifacts\incident-20261103-181348\runtime-43ba61d4'
if($betaBaseline.status -ne 'completed' -or $betaBaseline.passed -or $betaBaseline.exit -ne 1 -or !$betaBaseline.beta -or $betaBaseline.visual -or
 $betaBaseline.method -ne $betaMethod -or $betaBaseline.source -ne $pin -or @($betaBaseline.dirty).Count -or !$betaBaseline.binariesStable -or !$betaBaseline.sourceStable -or
 !$betaBaseline.gpuIsolated -or [IO.Path]::GetFullPath($betaBaseline.pluginRoot).TrimEnd('\') -ne $baselineRoot){throw 'Preserved43 Beta baseline receipt is not the exact matched startup experiment'}
$betaBaselineTrx=Join-Path (Split-Path -Parent $betaBaselinePath) 'transport.trx'
if((Get-FileHash -LiteralPath $betaBaselineTrx).Hash -ne $betaBaseline.trxHash){throw 'Preserved43 Beta TRX hash mismatch'}
foreach($trxPath in @($betaTrx,$betaBaselineTrx)) {
 [xml]$failedXml=Get-Content -LiteralPath $trxPath -Raw
 $failedCases=@($failedXml.TestRun.Results.UnitTestResult)
 if($failedCases.Count -ne 1 -or $failedCases[0].testName -ne $betaMethod -or $failedCases[0].outcome -ne 'Failed' -or
 [string]$failedCases[0].Output.ErrorInfo.Message -ne $betaFailure -or [string]$failedCases[0].Output.StdOut -notmatch 'UHD first composed native output:' -or
 [string]$failedCases[0].Output.StdOut -match 'TRANSPORT UI SUMMARY:|TRANSPORT VISUAL ONLY:'){throw 'Beta comparison is not the same failure before the UI phase'}
}
$prior43=Get-Content -LiteralPath (Join-Path $root 'artifacts\deployment-trial-43ba61d4\deployment.json') -Raw|ConvertFrom-Json
if($prior43.sourceCommit -ne '43ba61d4c8cfd24b65003d4c887413bd7969d031'){throw 'Prior43 identity mismatch'}
foreach($item in $betaBaseline.binaries){if((Get-FileHash -LiteralPath $item.path).Hash -ne $item.sha256){throw ('Baseline-tested file changed: '+$item.path)}}
foreach($relative in @('vprenderer\VideoProcessorVPRenderer.dll','vprenderer-beta\VideoProcessorVPRendererBeta.dll')) {
 $priorItem=@($prior43.files|Where-Object source -eq $relative)
 $baselineItem=@($betaBaseline.binaries|Where-Object path -eq (Join-Path $baselineRoot $relative))
 if($priorItem.Count -ne 1 -or $baselineItem.Count -ne 1 -or $priorItem[0].sha256 -ne $baselineItem[0].sha256){throw 'Preserved baseline is not the installed43 renderer'}
}
$baselineTest=@($betaBaseline.binaries|Where-Object path -eq (Join-Path $buildRoot 'VideoProcessor-Test.dll'))
if($baselineTest.Count -ne 1 -or $baselineTest[0].sha256 -ne (Get-FileHash -LiteralPath (Join-Path $buildRoot 'VideoProcessor-Test.dll')).Hash){throw 'Beta baseline did not use the current matching test DLL'}
$betaLimitation=[ordered]@{status='SameStartupFailureBeforeUiOn43And94';uiPhasePassed=$false;candidateEvidence=$betaPath;candidateEvidenceSha256=(Get-FileHash -LiteralPath $betaPath).Hash;candidateTrxSha256=(Get-FileHash -LiteralPath $betaTrx).Hash;baselineEvidence=$betaBaselinePath;baselineEvidenceSha256=(Get-FileHash -LiteralPath $betaBaselinePath).Hash;baselineTrxSha256=(Get-FileHash -LiteralPath $betaBaselineTrx).Hash;failure=$betaFailure;interpretation='Existing startup readiness failure is reproduced on both runtimes. Beta transport UI behavior is unqualified; active Standard renderer has its own passing focused test.'}
foreach($run in $native.runs) {
 [xml]$nativeXml=Get-Content -LiteralPath (Join-Path $root $run.trx) -Raw
 $nativeCases=@($nativeXml.TestRun.Results.UnitTestResult)
 if($nativeCases.Count -ne $run.executed -or @($nativeCases|Where-Object outcome -ne 'Passed').Count){throw 'Native TRX is incomplete or failing'}
}
$private=Get-Content -LiteralPath $privatePath -Raw|ConvertFrom-Json
$expected=@('StandardRendererCapturedV210CueHasNoCadenceFlicker','BetaRendererCapturedV210CueHasNoCadenceFlicker')
if($private.status -ne 'completed' -or !$private.passed -or $private.testExit -ne 0 -or
 $private.testSource -ne $pin -or $private.endSource -ne $pin -or $private.dirty -or !$private.sourceStable -or
 !$private.binariesStable -or !$private.gpuIsolated -or !$private.captureExercised -or
 @($private.results).Count -ne 2 -or @($private.results|Where-Object {$_.outcome -ne 'Passed' -or !$_.captureExercised}).Count -or
 @($expected|Where-Object {$_ -notin @($private.results.name)}).Count){throw 'Required real-renderer capture evidence is missing or does not match candidate'}
if([IO.Path]::GetFullPath($private.pluginRoot).TrimEnd('\') -ne $buildRoot){throw 'Private capture did not exercise this Release runtime'}
$privateTrx=Join-Path (Split-Path -Parent $privatePath) 'captured.trx'
if((Get-FileHash -LiteralPath $privateTrx).Hash -ne $private.trxHash){throw 'Private capture TRX hash mismatch'}
[xml]$privateXml=Get-Content -LiteralPath $privateTrx -Raw
$privateCases=@($privateXml.TestRun.Results.UnitTestResult)
if($privateCases.Count -ne 2 -or @($privateCases|Where-Object {$_.outcome -ne 'Passed'}).Count -or
 @($expected|Where-Object {$_ -notin @($privateCases.testName)}).Count){throw 'Private TRX must contain both passing renderer variants'}
foreach($case in $privateCases){if([string]$case.Output.StdOut -notmatch 'CAPTURED V210: input_hz='){throw 'Private TRX lacks actual recording coverage'}}
foreach($item in $private.binaries){if((Get-FileHash -LiteralPath $item.path).Hash -ne $item.sha256){throw ('Private-tested runtime changed: '+$item.path)}}
$expectedPrivateFiles=@((Join-Path $buildRoot 'VideoProcessor-Test.dll'),(Join-Path $buildRoot 'vprenderer\VideoProcessorVPRenderer.dll'),(Join-Path $buildRoot 'vprenderer-beta\VideoProcessorVPRendererBeta.dll'))
if(@($private.binaries).Count -ne 3 -or @($expectedPrivateFiles|Where-Object {$_ -notin @($private.binaries.path)}).Count){throw 'Unexpected private runtime evidence inventory'}
$qualificationError=$null
try { & (Join-Path $root 'tools\assert_runtime_qualification.ps1') -Root $root } catch { $qualificationError=$_.Exception.Message }
if(Get-Process VideoProcessor -ErrorAction SilentlyContinue){throw 'VP is running; cannot replace loaded runtime'}
foreach($rel in @('VideoProcessor-GUI.exe','vprenderer\VideoProcessorVPRenderer.dll','vprenderer-beta\VideoProcessorVPRendererBeta.dll')) {
 $path=Join-Path $buildRoot $rel;$meta=Get-Content -LiteralPath ($path+'.runtime.json') -Raw|ConvertFrom-Json
 if($meta.configuration -ne 'Release' -or $meta.architecture -ne 'x64' -or $meta.sha256 -ne (Get-FileHash -LiteralPath $path).Hash){throw ('Invalid x64 Release sidecar: '+$rel)}
 if(![Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($path)).Contains($pin)){throw ('Embedded source pin mismatch: '+$rel)}
}
$prior=Get-Content -LiteralPath (Join-Path $root 'artifacts\deployment-trial-43ba61d4\deployment.json') -Raw|ConvertFrom-Json
if($prior.sourceCommit -ne '43ba61d4c8cfd24b65003d4c887413bd7969d031' -or @($prior.files).Count -ne 28){throw 'Unexpected prior installed deployment receipt'}
$files=@(foreach($item in $prior.files){
 $source=[IO.Path]::GetFullPath((Join-Path $buildRoot $item.source));$target=[IO.Path]::GetFullPath((Join-Path $deployRoot $item.target))
 if(!$source.StartsWith($buildRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or !$target.StartsWith($deployRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Deployment target escaped expected root'}
 $before=(Get-FileHash -LiteralPath $target).Hash
 if($before -ne $item.sha256){throw ('Installed file no longer matches prior trial: '+$item.target)}
 $hash=(Get-FileHash -LiteralPath $source).Hash
 if($before -ne $hash){$lease=[IO.File]::Open($target,'Open','ReadWrite','None');$lease.Dispose()}
 [ordered]@{source=$item.source;target=$item.target;sha256=$hash;before=$before;action=$(if($hash -eq $before){'already-matching'}else{'copy'})}
})
if($files.Count -ne 28){throw 'Unexpected inventory'}
$backup=Join-Path $deployRoot ('backups\trial-94c4419a-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup|Out-Null
foreach($item in $files){$saved=Join-Path $backup $item.target;New-Item -ItemType Directory -Force -Path (Split-Path -Parent $saved)|Out-Null;Copy-Item -LiteralPath (Join-Path $deployRoot $item.target) -Destination $saved}
$configHashes=@{}
foreach($name in @('VideoProcessor.cfg','VideoProcessor.state')){$path=Join-Path $deployRoot $name;$configHashes[$name]=(Get-FileHash -LiteralPath $path).Hash;Copy-Item -LiteralPath $path -Destination (Join-Path $backup $name)}
foreach($item in $files){if((Get-FileHash -LiteralPath (Join-Path $backup $item.target)).Hash -ne $item.before){throw ('Backup verification failed: '+$item.target)}}
foreach($name in $configHashes.Keys){if((Get-FileHash -LiteralPath (Join-Path $backup $name)).Hash -ne $configHashes[$name]){throw ('Configuration backup verification failed: '+$name)}}
try {
 foreach($item in $files){if($item.action -eq 'copy'){Copy-Item -LiteralPath (Join-Path $buildRoot $item.source) -Destination (Join-Path $deployRoot $item.target) -Force}}
 foreach($item in $files){if((Get-FileHash -LiteralPath (Join-Path $deployRoot $item.target)).Hash -ne $item.sha256){throw ('Deployed hash mismatch: '+$item.target)}}
 foreach($name in $configHashes.Keys){if((Get-FileHash -LiteralPath (Join-Path $deployRoot $name)).Hash -ne $configHashes[$name]){throw ('Config changed during deployment: '+$name)}}
} catch {
 $deploymentFailure=$_
 foreach($item in $files){if($item.action -eq 'copy'){Copy-Item -LiteralPath (Join-Path $backup $item.target) -Destination (Join-Path $deployRoot $item.target) -Force}}
 foreach($item in $files){if((Get-FileHash -LiteralPath (Join-Path $deployRoot $item.target)).Hash -ne $item.before){throw ('Automatic runtime rollback verification failed: '+$item.target)}}
 [ordered]@{sourceCommit=$pin;status='DeploymentFailedRuntimeRolledBack';backup=$backup;error=($deploymentFailure|Out-String);configurationEdits=@();at=(Get-Date).ToString('o')}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'deployment-failure.json')
 throw $deploymentFailure
}
$receipt=[ordered]@{sourceCommit=$pin;sourceFingerprint=$identity.fingerprint;builtFromCleanSource=$true;configuration='Release';architecture='x64';deploymentStatus='UserAuthorizedLiveTrial';fullQualification=($null -eq $qualificationError);qualificationGate=$qualificationError;authorization='User explicitly requested deploy when ready; reversible manual live trial after complete Native, the active Standard actual transport-overlay case, and both captured-cue cases; matched preexisting Beta startup failure is explicitly retained. User directed focused iteration; broader integration checkpoint remains deferred.';focusedEvidence=$focusedPath;nativeEvidence=$nativePath;nativeExecuted=$nativeExpected;betaOverlayEvidence=$betaPath;betaUiPhasePassed=$false;betaStartupLimitation=$betaLimitation;standardOverlayEvidence=$focusedPath;standardOverlayEvidenceSha256=(Get-FileHash -LiteralPath $focusedPath).Hash;betaOverlayEvidenceSha256=(Get-FileHash -LiteralPath $betaPath).Hash;nativeEvidenceSha256=(Get-FileHash -LiteralPath $nativePath).Hash;broaderGpuSuiteRerun=$false;privateActualGpuEvidence=$privatePath;privateActualGpuEvidenceSha256=(Get-FileHash -LiteralPath $privatePath).Hash;priorDeploymentEvidence=(Join-Path $root 'artifacts\deployment-trial-43ba61d4\deployment.json');privateReplayCapture='analysis-20261103-164841-56444-1';knownLimitations=@('Beta transport-overlay fixture fails during initial readiness on both43 and94; its UI phase was not exercised or qualified','Full mandatory runtime qualification is incomplete unless qualificationGate is null','Broader motion/GPU suite and full actual runtime/cadence matrix were not rerun for this focused repair; changing-caption, interior UI overlap, and reset scenarios remain open','User acceptance on the complete live playback configuration remains open');backup=$backup;configurationEdits=@();configurationHashesBefore=$configHashes;configurationPreservedBeforeRestart=$true;files=$files;deployedAt=(Get-Date).ToString('o')}
$receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $PSScriptRoot 'deployment.json') -Encoding utf8
$receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $backup 'deployment.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'rollback.ps1') -Destination (Join-Path $backup 'rollback.ps1')
Write-Output ('TRIAL_DEPLOYMENT_VERIFIED source='+$pin+' files='+$files.Count+' backup='+$backup)
