# Native test result checks shared by the runner and its contract tests.
Set-StrictMode -Version Latest
function Read-VpTestResults([string]$Path) {
    [xml]$document = Get-Content -LiteralPath $Path -Raw
    $ns = [Xml.XmlNamespaceManager]::new($document.NameTable)
    $ns.AddNamespace('t', 'http://microsoft.com/schemas/VisualStudio/TeamTest/2010')
    $summary = $document.SelectSingleNode('/t:TestRun/t:ResultSummary', $ns)
    $counts = $document.SelectSingleNode('/t:TestRun/t:ResultSummary/t:Counters', $ns)
    $results = @($document.SelectNodes('/t:TestRun/t:Results/t:UnitTestResult', $ns))
    if (!$summary -or !$counts -or $summary.outcome -ne 'Completed' -or
        $results.Count -eq 0 -or [int]$counts.total -ne $results.Count -or
        [int]$counts.executed -ne $results.Count -or [int]$counts.passed -ne $results.Count -or
        @($results | Where-Object { $_.outcome -ne 'Passed' }).Count) {
        throw 'Test run failed, skipped tests, was aborted, or did not execute a complete nonempty set.'
    }
    $definitions = @{}
    foreach ($definition in $document.SelectNodes('/t:TestRun/t:TestDefinitions/t:UnitTest', $ns)) {
        $definitions[$definition.id] = $definition.TestMethod.className.Split('.')[-1]
    }
    $ids = @{}
    foreach ($result in $results) {
        if (!$definitions.ContainsKey($result.testId) -or $ids.ContainsKey($result.testId)) {
            throw 'Missing test definition or duplicate test result.'
        }
        $ids[$result.testId] = $true
        [pscustomobject]@{ class=$definitions[$result.testId]; method=$result.testName;
            seconds=([timespan]::Parse($result.duration)).TotalSeconds }
    }
}
function Assert-VpTestSelection($Results, [string[]]$Classes, $Methods, $ExcludedMethods=@()) {
    foreach ($class in $Classes) {
        if (!@($Results | Where-Object { $_.class -ceq $class }).Count) {
            throw "Selected class did not execute: $class"
        }
    }
    foreach ($method in $Methods) {
        if (@($Results | Where-Object { $_.class -ceq $method.class -and $_.method -ceq $method.method }).Count -ne 1) {
            throw "Selected method did not execute exactly once: $($method.class)::$($method.method)"
        }
    }
    foreach ($excluded in $ExcludedMethods) {
        if (@($Results | Where-Object { $_.class -ceq $excluded.class -and $_.method -ceq $excluded.method }).Count) {
            throw "Optional diagnostic unexpectedly selected: $($excluded.class)::$($excluded.method)"
        }
    }
    foreach ($result in $Results) {
        if ($result.class -cnotin $Classes -and !@($Methods | Where-Object {
            $_.class -ceq $result.class -and $_.method -ceq $result.method }).Count) {
            throw "Unexpected test selected: $($result.class)::$($result.method)"
        }
    }
}
