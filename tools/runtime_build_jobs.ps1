# Dependency barriers belong to the caller. Each batch may only contain projects
# with separate intermediate/output files and no shared generated source writes.
function Invoke-VpBuildBatch([string]$MSBuildPath, [object[]]$Projects, [string[]]$Common,
    [string]$Root, [string]$RunDirectory, [ValidateRange(1,4)][int]$Parallelism=2) {
    $pending=[Collections.Generic.Queue[object]]::new()
    foreach($project in $Projects){$pending.Enqueue($project)}
    $active=@();$records=@();$failed=$false
    try {
        while($pending.Count -or $active.Count) {
            while(-not $failed -and $pending.Count -and $active.Count -lt $Parallelism) {
                $project=$pending.Dequeue()
                $arguments=@((Join-Path $Root $project.path))+$Common+$project.extra
                $log=Join-Path $RunDirectory ($project.name+'-build.log')
                Write-Host "Build: $($project.name) (up to $Parallelism independent projects)"
                $job=Start-Job -ScriptBlock {
                    param($Helper,$Exe,$NativeArguments,$Log,$Name,$ProjectPath)
                    $ErrorActionPreference='Stop'
                    . $Helper
                    $timer=[Diagnostics.Stopwatch]::StartNew()
                    $code=Invoke-VpQualificationNativeProcess $Exe $NativeArguments $Log
                    [pscustomobject]@{name=$Name;project=$ProjectPath;exit=$code;seconds=[Math]::Round($timer.Elapsed.TotalSeconds,3);log=$Log}
                } -ArgumentList (Join-Path $Root 'tools/runtime_qualification.ps1'),$MSBuildPath,$arguments,$log,$project.name,$project.path
                $active+=,$job
            }
            if(-not $active.Count){break}
            $null=Wait-Job -Job $active -Any -Timeout 1
            foreach($job in @($active | Where-Object {$_.State -in @('Completed','Failed','Stopped')})) {
                if($job.State -ne 'Completed'){throw "Build worker failed: $($job.ChildJobs[0].JobStateInfo.Reason)"}
                $record=Receive-Job -Job $job -ErrorAction Stop
                if(-not $record -or $null -eq $record.exit){throw 'Build worker returned no result.'}
                $records+=,$record
                Write-Host "Build finished: $($record.name), $($record.seconds)s, exit $($record.exit)"
                if($record.exit -ne 0){$failed=$true}
                $active=@($active | Where-Object {$_.Id -ne $job.Id})
                Remove-Job -Job $job
            }
        }
    } finally {
        # Drain started processes even after one failure; no orphaned compiler
        # may keep mutating artifacts after the caller reports a failed run.
        foreach($job in $active){$null=Wait-Job -Job $job;Remove-Job -Job $job}
        $records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $RunDirectory ('build-batch-'+[guid]::NewGuid().ToString('N')+'.json')) -Encoding UTF8
    }
    if($failed){throw "Companion build failed. See $RunDirectory"}
    return $records
}
