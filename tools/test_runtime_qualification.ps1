[CmdletBinding()]
param([string]$Root, [string]$NativeTrx)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'runtime_qualification.ps1')
$Root=Get-VpQualificationRoot $Root
foreach($relative in @('tools/runtime_qualification.ps1','tools/qualify_runtime_build.ps1','tools/assert_runtime_qualification.ps1','tools/test_runtime_qualification.ps1','tools/build_installer.ps1','tools/package_release.ps1')) {
    $tokens=$null;$errors=$null
    $null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $Root $relative),[ref]$tokens,[ref]$errors)
    if($errors.Count){throw ($errors|Out-String)}
}
$macroFixture=@(Get-VpTestMacroTokens 'TEST_CLASS(First) { TEST_METHOD(One) {} TEST_METHOD(Two) {} }; TEST_CLASS(Second) { } TEST_METHOD(Three) {}')
$macroSequence=($macroFixture|ForEach-Object {$_.kind+':'+$_.name}) -join ','
if($macroSequence -ne 'CLASS:First,METHOD:One,METHOD:Two,CLASS:Second,METHOD:Three'){throw 'Inline/multiple macro tokens were dropped or reordered'}
Write-Host 'PASS: inline multiple-method and multiple-class macro order preserved'
$realPlan=Get-VpCriticalTestPlan $Root
Write-Host "Manifest discovered $($realPlan.nativeCount) native methods and $($realPlan.actualCount) mandatory actual-renderer methods."
if($NativeTrx) {
    $nativeExpected=@{}
    foreach($key in $realPlan.expected.Keys){if($realPlan.expected[$key] -eq 'native'){$nativeExpected[$key]='native'}}
    $nativePlan=[pscustomobject]@{expected=$nativeExpected;nativeCount=$realPlan.nativeCount;actualCount=0}
    $nativeProof=Read-VpCriticalTrx $NativeTrx $nativePlan
    Write-Host "PASS: actual native TRX matches all $($nativeProof.executed) discovered native contracts exactly."
}

function MustReject([string]$Name,[string]$Pattern,[scriptblock]$Action) {
    $message='';try { & $Action } catch {$message=$_.Exception.Message}
    if(-not $message -or $message -notlike $Pattern){throw "Gate negative '$Name' did not reject for expected reason '$Pattern': $message"}
    Write-Host "PASS: $Name rejected"
}
MustReject 'Debug cannot inherit Release qualification' '*supports Release|x64*' {Assert-VpQualificationConfiguration 'Debug' 'x64'}
MustReject 'Win32 cannot inherit x64 qualification' '*supports Release|x64*' {Assert-VpQualificationConfiguration 'Release' 'Win32'}
Assert-VpSameOutputInventory @([pscustomobject]@{path='runtime.dll';sha256='AAA'}) @([pscustomobject]@{path='runtime.dll';sha256='AAA'})
MustReject 'changed runtime hash' '*output changed*' {Assert-VpSameOutputInventory @([pscustomobject]@{path='runtime.dll';sha256='AAA'}) @([pscustomobject]@{path='runtime.dll';sha256='BBB'})}
MustReject 'missing runtime output' '*inventory changed*' {Assert-VpSameOutputInventory @([pscustomobject]@{path='runtime.dll';sha256='AAA'}) @()}
$tempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
$tempPath=[IO.Path]::GetFullPath((Join-Path $tempRoot ('vp-qualification-contract-'+[Guid]::NewGuid().ToString('N'))))
if(-not $tempPath.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -or $tempPath -eq $tempRoot.TrimEnd('\')){throw 'Unsafe fixture directory'}
$null=New-Item -ItemType Directory -Path $tempPath
try {
    $nativeHost=Join-Path $PSHOME $(if($PSVersionTable.PSEdition -eq 'Desktop'){'powershell.exe'}else{'pwsh.exe'})
    foreach($exit in @(0,7)) {
        $log=Join-Path $tempPath ('stderr-exit-'+$exit+'.log')
        $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes(('[Console]::Error.WriteLine("qualification-native-stderr");exit '+$exit)))
        $processArgs=@('-NoProfile','-EncodedCommand',$encoded)
        $record=[ordered]@{testExit=(Invoke-VpQualificationNativeProcess $nativeHost $processArgs $log);log=$log}
        if($record.testExit -ne $exit -or (Get-Content -LiteralPath $log -Raw) -notmatch 'qualification-native-stderr') {throw 'Native stderr/exit evidence was lost'}
        if($ErrorActionPreference -ne 'Stop'){throw 'Native process helper did not restore terminating error policy'}
        Write-Host "PASS: stderr with exit $exit recorded without losing exit/log evidence"
    }
    $plan=[pscustomobject]@{expected=@{'NativeClass::NativeContract'='native';'ActualClass::ActualClassic'='actual-classic'};nativeCount=1;actualCount=1}
    function WriteFixture([string]$Outcome='Passed',[switch]$Missing,[switch]$Duplicate,[switch]$BadCounters) {
        $native='<UnitTestResult testId="n" testName="NativeContract" outcome="Passed" />'
        $actual=if($Missing){''}else{'<UnitTestResult testId="a" testName="ActualClassic" outcome="'+$Outcome+'" />'}
        if($Duplicate){$actual+=$actual}
        $count=if($BadCounters){0}elseif($Missing){1}elseif($Duplicate){3}else{2}
        $xml='<TestRun><Results>'+$native+$actual+'</Results><TestDefinitions><UnitTest id="n"><TestMethod className="Tests.NativeClass" /></UnitTest><UnitTest id="a"><TestMethod className="Tests.ActualClass" /></UnitTest></TestDefinitions><ResultSummary><Counters total="'+$count+'" executed="'+$count+'" passed="'+$count+'" failed="0" error="0" notExecuted="0" aborted="0" /></ResultSummary></TestRun>'
        $file=Join-Path $tempPath 'test.trx';$xml|Set-Content -LiteralPath $file;return $file
    }
    $good=Read-VpCriticalTrx (WriteFixture) $plan
    if($good.executed -ne 2 -or $good.native -ne 1 -or $good.actualClassic -ne 1){throw 'Valid execution counts were not preserved'}
    MustReject 'missing mandatory actual-renderer execution' '*did not execute*' {Read-VpCriticalTrx (WriteFixture -Missing) $plan}
    MustReject 'failed actual-renderer test' '*failed/not executed*' {Read-VpCriticalTrx (WriteFixture -Outcome 'Failed') $plan}
    MustReject 'not-executed actual-renderer test' '*failed/not executed*' {Read-VpCriticalTrx (WriteFixture -Outcome 'NotExecuted') $plan}
    MustReject 'duplicate execution cannot replace missing coverage' '*more than once*' {Read-VpCriticalTrx (WriteFixture -Duplicate) $plan}
    MustReject 'inconsistent execution counters' '*counters*' {Read-VpCriticalTrx (WriteFixture -BadCounters) $plan}
    $tools=Join-Path $tempPath 'tools';$null=New-Item -ItemType Directory -Path $tools
    '# No source lookup is reached when the receipt is missing.'|Set-Content -LiteralPath (Join-Path $tools 'installer_build_identity.ps1')
    MustReject 'missing qualification receipt' '*receipt is missing*' {Assert-VpRuntimeQualification $tempPath}
    # End-to-end validator fixture uses real Git content identity, canonical output
    # inventory and all current named tests; it never creates a receipt in the repo.
    Copy-Item -LiteralPath (Join-Path $Root 'tools\installer_build_identity.ps1') -Destination $tools -Force
    Copy-Item -LiteralPath (Join-Path $Root 'tools\critical_runtime_tests.json') -Destination $tools
    foreach($relative in @($realPlan.manifest.nativeSources)+@($realPlan.manifest.actualSource)) {
        $dest=Join-Path $tempPath $relative
        $null=New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest)
        Copy-Item -LiteralPath (Join-Path $Root $relative) -Destination $dest
    }
    "artifacts/`nx64/"|Set-Content -LiteralPath (Join-Path $tempPath '.gitignore')
    & git -C $tempPath init --quiet
    & git -C $tempPath add .
    & git -C $tempPath -c user.name=QualificationContract -c user.email=qualification@example.invalid commit --quiet -m fixture
    if($LASTEXITCODE -ne 0){throw 'Unable to initialize isolated source identity fixture'}
    foreach($relative in @('VideoProcessor-GUI.exe','VideoProcessor-Lib.lib','vprenderer/VideoProcessorVPRenderer.dll','vprenderer-beta/VideoProcessorVPRendererBeta.dll','VideoProcessor-Test.dll','tests-beta/VideoProcessor-TestBeta.dll','libplacebo-360.dll','tests-beta/libplacebo-388.dll','vprenderer/libplacebo-360.dll','vprenderer-beta/libplacebo-388.dll')) {
        $dest=Join-Path $tempPath ('x64/Release/'+$relative)
        $null=New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dest)
        'synthetic contract binary'|Set-Content -LiteralPath $dest
    }
    $fixturePlan=Get-VpCriticalTestPlan $tempPath
    $evidence=Join-Path $tempPath 'artifacts\qualification\runs\fixture'
    $null=New-Item -ItemType Directory -Force -Path $evidence
    function WriteFullTrx([switch]$MissingActual,[switch]$FailedActual) {
        $defs=[Text.StringBuilder]::new();$results=[Text.StringBuilder]::new();$i=0;$changed=$false
        foreach($key in ($fixturePlan.expected.Keys|Sort-Object)) {
            if($MissingActual -and -not $changed -and $fixturePlan.expected[$key] -eq 'actual-classic'){$changed=$true;continue}
            $parts=$key -split '::';$outcome='Passed'
            if($FailedActual -and -not $changed -and $fixturePlan.expected[$key] -eq 'actual-classic'){$changed=$true;$outcome='Failed'}
            $null=$defs.Append('<UnitTest id="'+$i+'"><TestMethod className="Tests.'+$parts[0]+'" /></UnitTest>')
            $null=$results.Append('<UnitTestResult testId="'+$i+'" testName="'+$parts[1]+'" outcome="'+$outcome+'" />');$i++
        }
        $xml='<TestRun><Results>'+$results+'</Results><TestDefinitions>'+$defs+'</TestDefinitions><ResultSummary><Counters total="'+$i+'" executed="'+$i+'" passed="'+$i+'" failed="0" error="0" notExecuted="0" aborted="0" /></ResultSummary></TestRun>'
        foreach($variant in @('standard','beta')){$xml|Set-Content -LiteralPath (Join-Path $evidence ($variant+'.trx'))}
    }
    WriteFullTrx
    . (Join-Path $tempPath 'tools\installer_build_identity.ps1')
    $receipt=[ordered]@{schemaVersion=1;status='Passed';configuration='Release';platform='x64';source=(Get-VpSourceIdentity $tempPath);manifestHash=$fixturePlan.manifestHash;binaries=(Get-VpQualifiedOutputInventory $tempPath);builds=@(@('native-library','host','standard-renderer','beta-renderer','standard-tests','beta-tests')|ForEach-Object {[ordered]@{name=$_;exit=0}});runs=@()}
    function SaveFixtureReceipt {
        $receipt.runs=@(foreach($variant in @('standard','beta')){[ordered]@{variant=$variant;buildExit=0;testExit=0;trx=('artifacts/qualification/runs/fixture/'+$variant+'.trx');trxSha256=(Get-FileHash -LiteralPath (Join-Path $evidence ($variant+'.trx'))).Hash;executed=$fixturePlan.expected.Count;actualClassic=6}})
        $receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Get-VpQualificationReceiptPath $tempPath)
    }
    SaveFixtureReceipt
    $null=Assert-VpRuntimeQualification $tempPath (Join-Path $tempPath 'x64/Release/VideoProcessor-GUI.exe')
    $receipt.runs[0].testExit=7
    $receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Get-VpQualificationReceiptPath $tempPath)
    MustReject 'recorded native nonzero exit fails qualification' '*Failed renderer dependency qualification*' {Assert-VpRuntimeQualification $tempPath}
    SaveFixtureReceipt

    'untracked production input'|Set-Content -LiteralPath (Join-Path $tempPath 'new-input.cpp')
    MustReject 'stale dirty source content' '*source commit/content digest differs*' {Assert-VpRuntimeQualification $tempPath}
    Remove-Item -LiteralPath (Join-Path $tempPath 'new-input.cpp')
    $binary=Join-Path $tempPath 'x64/Release/VideoProcessor-GUI.exe'
    'changed'|Set-Content -LiteralPath $binary
    MustReject 'end-to-end changed runtime binary' '*output changed*' {Assert-VpRuntimeQualification $tempPath}
    'synthetic contract binary'|Set-Content -LiteralPath $binary
    $receipt.status='Failed';SaveFixtureReceipt
    MustReject 'nonpassing qualification receipt' '*failed or unsupported*' {Assert-VpRuntimeQualification $tempPath}
    $receipt.status='Passed';WriteFullTrx -MissingActual;SaveFixtureReceipt
    MustReject 'receipt missing named actual execution' '*did not execute*' {Assert-VpRuntimeQualification $tempPath}
    WriteFullTrx -FailedActual;SaveFixtureReceipt
    MustReject 'receipt contains failing named actual execution' '*failed/not executed*' {Assert-VpRuntimeQualification $tempPath}
    WriteFullTrx;SaveFixtureReceipt
    '<changed />'|Set-Content -LiteralPath (Join-Path $evidence 'beta.trx')
    MustReject 'tampered TRX evidence' '*evidence changed*' {Assert-VpRuntimeQualification $tempPath}
    WriteFullTrx;SaveFixtureReceipt
    $receipt.runs=@($receipt.runs[0]);$receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Get-VpQualificationReceiptPath $tempPath)
    MustReject 'missing beta qualification' '*Both renderer dependency variants*' {Assert-VpRuntimeQualification $tempPath}

} finally {
    # One shell and a verified absolute temporary child, never a computed root.
    if($tempPath.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $tempPath).StartsWith('vp-qualification-contract-')) {
        Remove-Item -LiteralPath $tempPath -Recurse -Force
    }
}
Write-Host 'Qualification parser and fail-closed metadata contracts passed. No runtime build or GPU test was launched.'
