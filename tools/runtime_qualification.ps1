Set-StrictMode -Version Latest

function Get-VpQualificationRoot([string]$Root) {
    if (-not $Root) { $Root = Join-Path $PSScriptRoot '..' }
    return [IO.Path]::GetFullPath($Root).TrimEnd('\')
}
function Get-VpQualificationReceiptPath([string]$Root) {
    return Join-Path (Get-VpQualificationRoot $Root) 'artifacts\qualification\runtime-Release-x64.json'
}
function Assert-VpQualificationConfiguration([string]$Configuration, [string]$Platform) {
    if ($Configuration -ne 'Release' -or $Platform -ne 'x64') {
        throw 'Runtime qualification currently supports Release|x64 with matching native tests and both renderer variants. Use MSBuild.exe VideoProcessor.sln /t:Build /m /p:Configuration=Release /p:Platform=x64 /p:MagewellSdkRoot=<SDK>. Other runtime configurations need matching test support; they cannot inherit a Release receipt.'
    }
}
function Get-VpTestMacroTokens([string]$Source) {
    # Read lexical tokens in source order, including adjacent macros and methods
    # following a closing brace. Line boundaries do not define C++ declarations.
    foreach ($token in [regex]::Matches($Source, '\bTEST_(CLASS|METHOD)\s*\(\s*(\w+)\s*\)')) {
        [pscustomobject]@{kind=$token.Groups[1].Value;name=$token.Groups[2].Value}
    }
}
function Get-VpCriticalTestPlan([string]$Root) {
    $manifestPath = Join-Path $Root 'tools\critical_runtime_tests.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 1 -or $manifest.nativeSources.Count -ne 2 -or
        $manifest.actualMethods.Count -ne 6 -or $manifest.minimumNativeTests -lt 600) {
        throw 'Invalid or incomplete mandatory runtime test manifest.'
    }
    $expected = @{}; $classes = @{}; $nativeMethods = @{}; $nativeCount = 0
    foreach ($relative in $manifest.nativeSources) {
        $class = ''
        foreach ($token in @(Get-VpTestMacroTokens (Get-Content -LiteralPath (Join-Path $Root $relative) -Raw))) {
            if ($token.kind -eq 'CLASS') {
                $class = $token.name; $classes[$class] = $true
            } else {
                if (-not $class) { throw "Unscoped test in $relative" }
                $method = $token.name; $key = '{0}::{1}' -f $class,$method
                if ($expected.ContainsKey($key)) { throw "Duplicate mandatory test: $key" }
                $expected[$key] = 'native'; $nativeMethods[$method] = $true; $nativeCount++
            }
        }
    }
    if ($nativeCount -lt $manifest.minimumNativeTests) { throw "Only $nativeCount native contracts were discovered; mandatory coverage was removed." }
    foreach ($method in $manifest.requiredNativeMethods) {
        if (-not $nativeMethods.ContainsKey([string]$method)) { throw "Missing critical native regression: $method" }
    }
    $actualSource = Get-Content -LiteralPath (Join-Path $Root $manifest.actualSource) -Raw
    foreach ($method in $manifest.actualMethods) {
        if ($method -notmatch '^\w+$' -or $actualSource -notmatch ('TEST_METHOD\s*\(\s*'+[regex]::Escape($method)+'\s*\)')) {
            throw "Missing nonoptional actual-renderer regression: $method"
        }
        $key = '{0}::{1}' -f $manifest.actualClass,$method
        if ($expected.ContainsKey($key)) { throw "Duplicate actual-renderer regression: $key" }
        $expected[$key] = 'actual-classic'
    }
    $filters = @($classes.Keys | Sort-Object | ForEach-Object { "FullyQualifiedName~$_" })
    $filters += @($manifest.actualMethods | ForEach-Object { "FullyQualifiedName~$_" })
    return [pscustomobject]@{ manifest=$manifest; manifestHash=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash;
        expected=$expected; nativeCount=$nativeCount; actualCount=$manifest.actualMethods.Count; filter=($filters -join '|') }
}
function Get-VpQualifiedOutputInventory([string]$Root) {
    $files = @(
        'x64/Release/VideoProcessor-GUI.exe',
        'x64/Release/VideoProcessor-Lib.lib',
        'x64/Release/vprenderer/VideoProcessorVPRenderer.dll',
        'x64/Release/vprenderer-beta/VideoProcessorVPRendererBeta.dll',
        'x64/Release/VideoProcessor-Test.dll',
        'x64/Release/tests-beta/VideoProcessor-TestBeta.dll',
        'x64/Release/libplacebo-360.dll',
        'x64/Release/tests-beta/libplacebo-388.dll',
        'x64/Release/vprenderer/libplacebo-360.dll',
        'x64/Release/vprenderer-beta/libplacebo-388.dll'
    )
    return @(foreach ($relative in $files) {
        $path = Join-Path $Root $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Mandatory qualification output missing: $relative" }
        [pscustomobject]@{ path=$relative; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
    })
}
function Assert-VpSameOutputInventory($Expected, $Actual) {
    $want = @{}; foreach ($entry in $Expected) {
        if ($want.ContainsKey([string]$entry.path)) { throw "Duplicate output identity: $($entry.path)" }
        $want[[string]$entry.path] = [string]$entry.sha256
    }
    if ($want.Count -ne @($Actual).Count) { throw 'Qualification output inventory changed.' }
    foreach ($entry in $Actual) {
        if (-not $want.ContainsKey([string]$entry.path) -or $want[[string]$entry.path] -ne $entry.sha256) {
            throw "Qualification output changed: $($entry.path)"
        }
    }
}
function Read-VpCriticalTrx([string]$Path, $Plan) {
    [xml]$trx = Get-Content -LiteralPath $Path -Raw
    $definitions = @{}
    foreach ($definition in @($trx.TestRun.TestDefinitions.UnitTest)) {
        $definitions[[string]$definition.id] = $definition
    }
    $seen = @{}; $native = 0; $actual = 0
    $results = @($trx.TestRun.Results.UnitTestResult)
    if ($results.Count -eq 0) { throw "No mandatory tests executed: $Path" }
    $counters=$trx.TestRun.ResultSummary.Counters
    if ([int]$counters.total -ne $results.Count -or [int]$counters.executed -ne $results.Count -or
        [int]$counters.passed -ne $results.Count -or [int]$counters.failed -ne 0 -or
        [int]$counters.error -ne 0 -or [int]$counters.notExecuted -ne 0 -or [int]$counters.aborted -ne 0) {
        throw 'Critical TRX counters do not describe a completely executed passing run.'
    }
    foreach ($result in $results) {
        if ([string]$result.outcome -ne 'Passed') { throw "Critical test failed/not executed: $($result.testName) [$($result.outcome)]" }
        if (-not $definitions.ContainsKey([string]$result.testId)) { throw "TRX result has no definition: $($result.testName)" }
        $definition = $definitions[[string]$result.testId]
        $class = ([string]$definition.TestMethod.className -split '[.:]+')[-1]
        $key = '{0}::{1}' -f $class,[string]$result.testName
        if (-not $Plan.expected.ContainsKey($key)) { throw "Unexpected/misidentified critical test: $key" }
        if ($seen.ContainsKey($key)) { throw "Critical test executed more than once: $key" }
        $seen[$key] = $true
        if ($Plan.expected[$key] -eq 'native') { $native++ } else { $actual++ }
    }
    foreach ($key in $Plan.expected.Keys) { if (-not $seen.ContainsKey($key)) { throw "Mandatory test did not execute: $key" } }
    if ($native -ne $Plan.nativeCount -or $actual -ne $Plan.actualCount -or $results.Count -ne $Plan.expected.Count) {
        throw 'Critical test execution counts do not match the current source manifest.'
    }
    return [pscustomobject]@{ executed=$results.Count; native=$native; actualClassic=$actual; passed=$results.Count }
}
function Assert-VpRuntimeQualification([string]$Root, [string]$RequiredBinaryPath = '') {
    $Root = Get-VpQualificationRoot $Root
    . (Join-Path $Root 'tools\installer_build_identity.ps1')
    $receiptPath = Get-VpQualificationReceiptPath $Root
    if (-not (Test-Path -LiteralPath $receiptPath -PathType Leaf)) { throw 'Runtime build is UNQUALIFIED: mandatory qualification receipt is missing. Build Release|x64 through MSBuild; passing tests are required.' }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    if ($receipt.schemaVersion -ne 1 -or $receipt.status -ne 'Passed' -or
        $receipt.configuration -ne 'Release' -or $receipt.platform -ne 'x64') { throw 'Runtime build is UNQUALIFIED: failed or unsupported qualification receipt.' }
    $identity = Get-VpSourceIdentity $Root
    if ($receipt.source.commit -ne $identity.commit -or $receipt.source.fingerprint -ne $identity.fingerprint -or
        $receipt.source.dirty -ne $identity.dirty) { throw 'Runtime qualification is STALE: current source commit/content digest differs.' }
    $plan = Get-VpCriticalTestPlan $Root
    if ($receipt.manifestHash -ne $plan.manifestHash) { throw 'Runtime qualification is STALE: required regression manifest changed.' }
    $inventory = Get-VpQualifiedOutputInventory $Root
    Assert-VpSameOutputInventory $receipt.binaries $inventory
    if ($RequiredBinaryPath) {
        $required = [IO.Path]::GetFullPath($RequiredBinaryPath)
        if (-not @($inventory | Where-Object { [IO.Path]::GetFullPath((Join-Path $Root $_.path)) -eq $required }).Count) {
            throw "Runtime output was not qualified: $required. Custom output layouts need matching qualification support."
        }
    }
    $requiredBuilds=@('native-library','host','standard-renderer','beta-renderer','standard-tests','beta-tests')
    if (@($receipt.builds).Count -ne 6 -or @($receipt.builds | Where-Object { $_.exit -ne 0 }).Count -ne 0 -or
        @($receipt.builds | Select-Object -ExpandProperty name -Unique).Count -ne 6 -or
        @($receipt.builds | Where-Object { $_.name -notin $requiredBuilds }).Count -ne 0) {
        throw 'All six mandatory companion build steps must have succeeded.'
    }
    if (@($receipt.runs).Count -ne 2 -or @($receipt.runs | Select-Object -ExpandProperty variant -Unique).Count -ne 2) { throw 'Both renderer dependency variants must qualify.' }
    foreach ($run in $receipt.runs) {
        if ($run.variant -notin @('standard','beta') -or $run.buildExit -ne 0 -or $run.testExit -ne 0) { throw 'Failed renderer dependency qualification.' }
        $trx = [IO.Path]::GetFullPath((Join-Path $Root $run.trx))
        $allowed = (Join-Path $Root 'artifacts\qualification\runs').TrimEnd('\')+'\'
        if (-not $trx.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Qualification TRX is outside its evidence directory.' }
        if ((Get-FileHash -LiteralPath $trx -Algorithm SHA256).Hash -ne $run.trxSha256) { throw "Qualification evidence changed: $trx" }
        $counts = Read-VpCriticalTrx $trx $plan
        if ($counts.executed -ne $run.executed -or $counts.actualClassic -ne 6) { throw 'Qualification execution receipt differs from its TRX.' }
    }
    return $receipt
}
