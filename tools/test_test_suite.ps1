# Contract checks for fail-closed test result validation; no VP runtime launched.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'test_suite_support.ps1')
$root=Split-Path -Parent $PSScriptRoot
$output=Join-Path $root ('artifacts\test-runner-contract-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output | Out-Null
$fixture=Join-Path $output 'fixture.trx'
$xml=@"
<TestRun xmlns="http://microsoft.com/schemas/VisualStudio/TeamTest/2010">
 <Results>
  <UnitTestResult testId="a" testName="One" outcome="Passed" duration="00:00:00.1000000" />
  <UnitTestResult testId="b" testName="Two" outcome="Passed" duration="00:00:00.2000000" />
 </Results>
 <TestDefinitions>
  <UnitTest id="a"><TestMethod className="Tests.AlphaTests" /></UnitTest>
  <UnitTest id="b"><TestMethod className="VideoProcessorTest.BetaTests" /></UnitTest>
 </TestDefinitions>
 <ResultSummary outcome="Completed"><Counters total="2" executed="2" passed="2" /></ResultSummary>
</TestRun>
"@
$script:passed=0
function Expect-Failure([string]$Name, [scriptblock]$Action) {
    $failed=$false
    try { & $Action | Out-Null } catch { $failed=$true }
    if (!$failed) { throw "Contract failed to reject: $Name" }
    $script:passed++
    Write-Host "PASS rejects $Name"
}
function Write-Fixture([string]$Content) { $Content | Set-Content -LiteralPath $fixture -Encoding UTF8 }
Write-Fixture $xml
$results=@(Read-VpTestResults $fixture)
if ($results.Count -ne 2 -or [math]::Abs($results[0].seconds - 0.1) -gt 0.000001) { throw 'Passing results/timings were not parsed.' }
Assert-VpTestSelection $results @('AlphaTests') @([pscustomobject]@{class='BetaTests';method='Two'})
$script:passed++
Expect-Failure 'excluded diagnostic selected' { Assert-VpTestSelection $results @('AlphaTests','BetaTests') @() @([pscustomobject]@{class='AlphaTests';method='One'}) }
Expect-Failure 'missing selected class' { Assert-VpTestSelection $results @('MissingTests') @() }
Expect-Failure 'missing selected method' { Assert-VpTestSelection $results @('AlphaTests') @([pscustomobject]@{class='BetaTests';method='Gone'}) }
Expect-Failure 'unexpected selected test' { Assert-VpTestSelection $results @('AlphaTests') @() }
Expect-Failure 'partial requested selection' { Assert-VpTestSelection @($results[0]) @('AlphaTests','BetaTests') @() }
foreach ($outcome in @('Failed','NotExecuted','Inconclusive')) {
    Write-Fixture ($xml.Replace('testName="One" outcome="Passed"', ('testName="One" outcome="'+$outcome+'"')))
    Expect-Failure $outcome { Read-VpTestResults $fixture }
}
Write-Fixture ($xml.Replace('outcome="Completed"','outcome="Aborted"'))
Expect-Failure 'aborted run with passed partial results' { Read-VpTestResults $fixture }
Write-Fixture ($xml.Replace('total="2"','total="3"'))
Expect-Failure 'incomplete result count' { Read-VpTestResults $fixture }
Write-Fixture ($xml.Replace('executed="2"','executed="1"'))
Expect-Failure 'incomplete executed count' { Read-VpTestResults $fixture }
Write-Fixture ($xml.Replace('passed="2"','passed="1"'))
Expect-Failure 'incomplete passing count' { Read-VpTestResults $fixture }
Write-Fixture ($xml.Replace('testId="b"','testId="a"'))
Expect-Failure 'duplicate result identity' { Read-VpTestResults $fixture }
Write-Fixture ($xml.Replace('<UnitTest id="b">','<UnitTest id="missing">'))
Expect-Failure 'missing definition' { Read-VpTestResults $fixture }
Write-Fixture '<TestRun xmlns="http://microsoft.com/schemas/VisualStudio/TeamTest/2010"><Results/><ResultSummary outcome="Completed"><Counters total="0" executed="0" passed="0"/></ResultSummary></TestRun>'
Expect-Failure 'empty successful run' { Read-VpTestResults $fixture }
Write-Fixture 'not XML'
Expect-Failure 'malformed report' { Read-VpTestResults $fixture }
$runner=Join-Path $PSScriptRoot 'test_vp.ps1'
Expect-Failure 'Targeted without classes' { & $runner -Suite Targeted -List }
Expect-Failure 'classes silently ignored by Smoke' { & $runner -Classes AlphaTests -List }
Expect-Failure 'filter injection' { & $runner -Suite Targeted -Classes 'Alpha|FullyQualifiedName~Other' -List }
$smoke=& $runner -Suite Smoke -List
$full=& $runner -Suite Full -List
$targeted=& $runner -Suite Targeted -Classes AlphaTests,AlphaTests -List
if (!$smoke.classes.Count -or !$smoke.methods.Count -or $full.filter -or
    $targeted.classes.Count -ne 1 -or $targeted.filter -ne 'FullyQualifiedName~AlphaTests::') { throw 'Incorrect suite selection.' }
$script:passed++
Write-Host "PASS $script:passed runner contracts. Fixtures preserved: $output"
