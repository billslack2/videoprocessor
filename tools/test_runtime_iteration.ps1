[CmdletBinding()]
param([string]$Root)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'runtime_qualification.ps1')
. (Join-Path $PSScriptRoot 'runtime_iteration.ps1')
. (Join-Path $PSScriptRoot 'runtime_build_jobs.ps1')
$Root=Get-VpQualificationRoot $Root
function Require([bool]$Valid,[string]$Message){if(-not $Valid){throw $Message}}
function Reject([string]$Pattern,[scriptblock]$Action){
    $message='';try{& $Action}catch{$message=$_.Exception.Message}
    Require ($message -like $Pattern) "Expected rejection '$Pattern', got '$message'"
}
$plan=Get-VpCriticalTestPlan $Root
Reject '*cannot select a subset*' {Get-VpIterationTestPlan $plan Qualification @('not-a-regression')}
Reject '*requires explicit*' {Get-VpIterationTestPlan $plan Focused}
Reject '*Unknown required regression*' {Get-VpIterationTestPlan $plan Focused @('not-a-regression')}
$nativeKey=@($plan.expected.Keys | Where-Object {$plan.expected[$_] -eq 'native'} | Sort-Object)[0]
$actualKey=@($plan.expected.Keys | Where-Object {$plan.expected[$_] -ne 'native'} | Sort-Object)[0]
$native=($nativeKey -split '::')[-1];$actual=($actualKey -split '::')[-1]
Reject '*cannot run GPU method*' {Get-VpIterationTestPlan $plan Native @($actual)}
$selected=Get-VpIterationTestPlan $plan Focused @($native,$actual,$actual)
$expectedKeys=@($plan.expected.Keys | Where-Object {($_ -split '::')[-1] -in @($native,$actual)} | Sort-Object)
Require ((@($selected.expected.Keys | Sort-Object) -join '|') -eq ($expectedKeys -join '|')) 'Focused selection did not retain exactly its manifest subset.'
Require ($selected.nativeCount -eq 1 -and $selected.actualCount -eq 1) 'Focused selection counts wrong.'
Require ((@($selected.filter -split '\|')).Count -eq 2) 'Duplicate focused method duplicated execution filter.'
$allNative=Get-VpIterationTestPlan $plan Native
Require ($allNative.nativeCount -eq $plan.nativeCount -and $allNative.actualCount -eq 0 -and $allNative.filter -eq $plan.nativeFilter) 'Native lane lost required CPU methods or admitted GPU cases.'
Require ([object]::ReferenceEquals($plan,(Get-VpIterationTestPlan $plan Qualification))) 'Full qualification plan changed.'
Write-Host 'PASS: real manifest selection is exact; unknown/GPU/subset misuse fails closed.'
$tempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
$temp=[IO.Path]::GetFullPath((Join-Path $tempRoot ('vp-iteration-contract-'+[guid]::NewGuid().ToString('N'))))
Require ($temp.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -and $temp -ne $tempRoot.TrimEnd('\')) 'Unsafe fixture path.'
$null=New-Item -ItemType Directory -Path (Join-Path $temp 'tools') -Force
try {
    $sourceList=New-VpCriticalNativeSourceList $Root (Join-Path $temp 'native-sources.props')
    $manifest=Get-Content -LiteralPath (Join-Path $Root 'tools/critical_runtime_tests.json') -Raw | ConvertFrom-Json
    $requiredFiles=@('pch.cpp','..\VideoProcessor-Lib\vprenderer\LibplaceboRenderParameters.cpp')+@(@($manifest.nativeSources)+@($manifest.explicitNativeMethods | ForEach-Object {$_.source}) | ForEach-Object {Split-Path -Leaf $_})
    Require (($sourceList.files -join '|') -eq (@($requiredFiles | Sort-Object -Unique) -join '|')) 'Critical Native whitelist dropped or added translation units.'
    [xml]$sourceXml=Get-Content -LiteralPath $sourceList.path -Raw
    Require ([int]$sourceXml.Project.PropertyGroup.VPCriticalNativeCompileCount -eq $sourceList.count) 'Generated compile-source count is inconsistent.'
    Require (([string]$sourceXml.Project.PropertyGroup.VPCriticalNativeCompileFiles) -eq ($sourceList.files -join ';')) 'Generated MSBuild whitelist changed source membership.'
    Write-Host "PASS: manifest-derived Critical Native whitelist contains exactly $($sourceList.count) required sources including PCH."
    $realHelper=(Join-Path $Root 'tools/runtime_qualification.ps1').Replace("'","''")
    ". '$realHelper'" | Set-Content -LiteralPath (Join-Path $temp 'tools/runtime_qualification.ps1')
    @'
param([string]$Name,[string]$Trace,[string]$Dependency='',[int]$PauseMs=0,[int]$ExitCode=0,[string]$Value='',[string]$CommonToken='')
$ErrorActionPreference='Stop'
if($Dependency -and -not (Test-Path -LiteralPath $Dependency)){exit 91}
@{name=$Name;tick=[DateTime]::UtcNow.Ticks;value=$Value;common=$CommonToken;args=@($args)} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Trace ($Name+'.start.json'))
Start-Sleep -Milliseconds $PauseMs
@{name=$Name;tick=[DateTime]::UtcNow.Ticks} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $Trace ($Name+'.end.json'))
exit $ExitCode
'@ | Set-Content -LiteralPath (Join-Path $temp 'fake.ps1')
    $hostExe=Join-Path $PSHOME $(if($PSVersionTable.PSEdition -eq 'Desktop'){'powershell.exe'}else{'pwsh.exe'})
    function Project([string]$Name,[int]$Pause=0,[int]$Exit=0,[string]$Dependency='') {
        @{name=$Name;path='fake.ps1';extra=@('-Name',$Name,'-Trace',$temp,'-PauseMs',"$Pause",'-ExitCode',"$Exit",'-Value','literal spaces & $() "quotes"','-Dependency',$Dependency)}
    }
    $dependency=@(Invoke-VpBuildBatch $hostExe @((Project 'library' 60)) @('-CommonToken','shared literal & spaces') $temp $temp 1)
    Require ($dependency.Count -eq 1 -and $dependency[0].exit -eq 0) 'Dependency batch missing success.'
    $marker=Join-Path $temp 'library.end.json'
    $jobs=@((Project 'consumer-a' 1500 0 $marker),(Project 'consumer-b' 1500 0 $marker),(Project 'consumer-c' 20 0 $marker))
    $results=@(Invoke-VpBuildBatch $hostExe $jobs @('-CommonToken','shared literal & spaces') $temp $temp 2)
    Require ($results.Count -eq 3 -and -not @($results | Where-Object {$_.exit -ne 0}).Count) 'Parallel consumers missing success.'
    $times=@{};foreach($name in @('library','consumer-a','consumer-b','consumer-c')) {
        $times[$name]=@{start=(Get-Content -Raw (Join-Path $temp ($name+'.start.json')) | ConvertFrom-Json);end=(Get-Content -Raw (Join-Path $temp ($name+'.end.json')) | ConvertFrom-Json)}
        Require ($times[$name].start.value -eq 'literal spaces & $() "quotes"') 'Native argument quoting changed literal data.'
        Require ($times[$name].start.common -eq 'shared literal & spaces') 'Shared MSBuild arguments were dropped or reordered.'
    }
    Require ($times['consumer-a'].start.tick -ge $times.library.end.tick -and $times['consumer-b'].start.tick -ge $times.library.end.tick) 'Consumer crossed dependency barrier.'
    Require ($times['consumer-a'].start.tick -lt $times['consumer-b'].end.tick -and $times['consumer-b'].start.tick -lt $times['consumer-a'].end.tick) 'Independent consumers did not overlap.'
    Require ($times['consumer-c'].start.tick -ge [Math]::Min($times['consumer-a'].end.tick,$times['consumer-b'].end.tick)) 'Scheduler exceeded configured parallelism.'
    Write-Host 'PASS: actual cheap subprocesses preserve arguments, overlap only independent work, and respect dependency/concurrency barriers.'
    $failJobs=@((Project 'failure' 50 7),(Project 'drain' 2500),(Project 'must-not-start' 0))
    Reject '*Companion build failed*' {Invoke-VpBuildBatch $hostExe $failJobs @() $temp $temp 2}
    Require (Test-Path -LiteralPath (Join-Path $temp 'drain.end.json')) 'Failed batch returned while an already-started process could still write.'
    Require (-not (Test-Path -LiteralPath (Join-Path $temp 'must-not-start.start.json'))) 'Failed batch scheduled more pending work.'
    $records=@(Get-ChildItem -LiteralPath $temp -Filter 'build-batch-*.json' | ForEach-Object {Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json})
    Require (@($records | Where-Object {$_.name -eq 'failure' -and $_.exit -eq 7}).Count -eq 1) 'Failure exit evidence missing.'
    Write-Host 'PASS: nonzero failure prevents new scheduling, drains started writers, and retains exit evidence.'
} finally {Remove-Item -LiteralPath $temp -Recurse -Force}
