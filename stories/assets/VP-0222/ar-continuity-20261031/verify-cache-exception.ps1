param([string]$Validation='E:\codex\videoprocessor\ar-evidence-continuity-20261031\x64\Release\ar-validation')
$ErrorActionPreference='Stop'
$r=Get-Content (Join-Path $Validation 'cache-failure-review.json') -Raw | ConvertFrom-Json
$e=Get-Content (Join-Path $Validation 'tested-source.json') -Raw | ConvertFrom-Json
if($r.commit -ne '27dc733ad6b7f5afa01a13c1a114ff9337cc40a8' -or $e.commit -ne $r.commit -or $e.sourceDirty -or $e.testSha256 -ne $r.testSha256 -or (Get-FileHash $e.testBinary).Hash -ne $r.testSha256){throw 'Exception source/binary mismatch'}
foreach($s in @(@{name='final-full';hash=$r.fullTrxSha256},@{name='config-cache-isolated';hash=$r.isolatedTrxSha256})){
    if((Get-FileHash (Join-Path $Validation ($s.name+'.trx'))).Hash -ne $s.hash){throw 'Exception evidence changed'}
}
[xml]$full=Get-Content (Join-Path $Validation 'final-full.trx')
[xml]$isolated=Get-Content (Join-Path $Validation 'config-cache-isolated.trx')
$fail=@($full.TestRun.Results.UnitTestResult | Where-Object outcome -ne Passed)
$retry=@($isolated.TestRun.Results.UnitTestResult)
if($fail.Count -ne 1 -or $fail[0].testName -ne 'SameSizeEditWithRestoredWriteTimeInvalidatesCache' -or $fail[0].testId -ne $r.testId -or $full.TestRun.ResultSummary.Counters.total -ne 3392 -or $full.TestRun.ResultSummary.Counters.passed -ne 3391){throw 'Unexpected comprehensive failure'}
if($retry.Count -ne 5 -or @($retry | Where-Object outcome -ne Passed).Count -or !($retry | Where-Object testId -eq $r.testId)){throw 'Isolated cache review mismatch'}
$source=Split-Path (Split-Path (Split-Path $Validation))
foreach($p in $r.unchangedFromBeta.PSObject.Properties){
    foreach($rev in @($r.commit,'a591ad3b532771911e85f8ef026958279f2065f0')){
        if((& git -C $source rev-parse ($rev+':'+$p.Name)).Trim() -ne $p.Value){throw 'Cache exception is not unchanged beta code'}
    }
}
Write-Host 'Verified explicit existing cache exception: original full run remains 3391/3392; isolated cache class 5/5.'
