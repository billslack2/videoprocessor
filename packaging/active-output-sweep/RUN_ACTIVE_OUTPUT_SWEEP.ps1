[CmdletBinding()]
param(
    [ValidateSet('sdr', 'hdr')]
    [string]$Suite = 'sdr',
    [ValidateSet('rec709', 'bt2020')]
    [string]$TargetPrimaries = '',
    [string]$ReportBt2020 = '',
    [ValidateRange(1000, 600000)]
    [int]$HoldMs = 10000,
    [ValidateSet('capture', 'renderer')]
    [string]$Restart = 'capture',
    [string]$Tests = ''
)

$ErrorActionPreference = 'Stop'
$packageRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$probe = Join-Path $packageRoot 'VideoProcessorOutputProbe.exe'
$template = Join-Path $packageRoot ("active-output-sweep-{0}-template.cfg" -f $Suite)
$runConfig = Join-Path $packageRoot ("active-output-sweep-{0}-run.cfg" -f $Suite)
$videoProcessor = Join-Path $packageRoot 'VideoProcessor.exe'

if (-not (Test-Path -LiteralPath $probe -PathType Leaf) -or
    -not (Test-Path -LiteralPath $template -PathType Leaf) -or
    -not (Test-Path -LiteralPath $videoProcessor -PathType Leaf)) {
    throw "The $Suite active-output-sweep package is incomplete."
}
if ($Tests -and $Tests -notmatch '^\s*\d+(\s*-\s*\d+)?(\s*,\s*\d+(\s*-\s*\d+)?)*\s*$') {
    throw "Invalid -Tests '$Tests'. Use documented numbers/ranges, e.g. '2,5' or '2-5,8'."
}

function Stop-ExistingVideoProcessorProcesses {
    $processNames = @('VideoProcessor', 'VideoProcessorConfig')
    $existing = @(Get-Process -Name $processNames -ErrorAction SilentlyContinue)
    if ($existing.Count -eq 0) { return }
    Write-Host 'Closing existing VideoProcessor and configuration processes before the sweep:'
    foreach ($process in $existing) {
        Write-Host ("  {0} (PID {1})" -f $process.ProcessName, $process.Id)
        try { Stop-Process -Id $process.Id -Force -ErrorAction Stop }
        catch {
            throw "Could not close $($process.ProcessName) (PID $($process.Id)). Close it manually, then retry. $($_.Exception.Message)"
        }
    }
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 200
        $remaining = @(Get-Process -Name $processNames -ErrorAction SilentlyContinue)
    } while ($remaining.Count -gt 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($remaining.Count -gt 0) {
        throw 'An existing VideoProcessor or VideoProcessorConfig process did not exit within five seconds.'
    }
}

Stop-ExistingVideoProcessorProcesses

if ($Suite -eq 'hdr') {
    if (-not $TargetPrimaries) {
        Write-Host 'Choose the HDR target primaries for this whole sweep:'
        Write-Host '  [1] Rec.709 target'
        Write-Host '  [2] BT.2020 target'
        do {
            $answer = Read-Host 'Target primaries number'
            $TargetPrimaries = switch ($answer) {
                '1' { 'rec709' }
                '2' { 'bt2020' }
                default { '' }
            }
        } while (-not $TargetPrimaries)
    }
    if (-not $ReportBt2020) {
        do {
            $answer = Read-Host 'Send the BT.2020 HDMI InfoFrame flag? [y/n]'
            $ReportBt2020 = switch -Regex ($answer.Trim()) {
                '^(y|yes|true|1)$' { 'true' }
                '^(n|no|false|0)$' { 'false' }
                default { '' }
            }
        } while (-not $ReportBt2020)
    }
}

$monitors = @(& $probe --list-monitors | ForEach-Object {
    if ($_ -match '^(?<number>\d+): friendly=(?<friendly>.*?) source=(?<source>.*?) device=') {
        [pscustomobject]@{
            Number = [int]$matches.number
            FriendlyName = $matches.friendly.Trim()
            Source = $matches.source.Trim()
        }
    }
})
if ($monitors.Count -eq 0) { throw 'No active displays were found. The sweep was not started.' }
if ($monitors.Count -eq 1) {
    $selected = $monitors[0]
    Write-Host "Only one active display: $($selected.FriendlyName) ($($selected.Source))"
}
else {
    Write-Host 'Choose the fullscreen display for the VP live output sweep:'
    foreach ($monitor in $monitors) {
        Write-Host ("  [{0}] {1} ({2})" -f $monitor.Number,
            $monitor.FriendlyName, $monitor.Source)
    }
    do {
        $answer = Read-Host 'Display number'
        $number = 0
        $selected = if ([int]::TryParse($answer, [ref]$number)) {
            $monitors | Where-Object { $_.Number -eq $number }
        } else { $null }
    } while ($null -eq $selected)
}

# This generated disposable config is never the user's normal VP configuration.
Copy-Item -LiteralPath $template -Destination $runConfig -Force
$config = Get-Content -LiteralPath $runConfig -Raw
if ($config -match '(?m)^fullscreen_monitor_name:') {
    $config = [regex]::Replace($config, '(?m)^fullscreen_monitor_name:.*$',
        ('fullscreen_monitor_name: ' + $selected.FriendlyName))
}
else {
    $config = $config -replace '(?m)^\[general\]\r?\n',
        ("[general]`r`nfullscreen_monitor_name: " + $selected.FriendlyName + "`r`n")
}
if ($Suite -eq 'hdr') {
    $config = [regex]::Replace($config, '(?mi)^sdr_target_primaries:.*$',
        ('sdr_target_primaries: ' + $TargetPrimaries))
    $config = [regex]::Replace($config, '(?mi)^report_bt2020_to_display:.*$',
        ('report_bt2020_to_display: ' + $ReportBt2020))
}
[IO.File]::WriteAllText($runConfig, $config,
    [Text.UTF8Encoding]::new($false))

Write-Host "Starting fullscreen VP $Suite output sweep on $($selected.FriendlyName)."
if ($Suite -eq 'hdr') {
    Write-Host "Target primaries: $TargetPrimaries; BT.2020 InfoFrame: $ReportBt2020."
}
Write-Host $(if ($Tests) {
    "Selected tests: $Tests. Hold: $HoldMs ms per live test."
} else {
    "Selected tests: all documented cases. Hold: $HoldMs ms per live test."
})
Write-Host 'PASS/EXPECTED/MEASURE/FAIL comes from renderer state; MEASURE still needs visual or meter grading.'
$arguments = @(
    '--config', $runConfig,
    '/active_output_sweep',
    '/active_output_sweep_suite', $Suite,
    '/active_output_sweep_hold_ms', $HoldMs,
    '/active_output_sweep_show_info', 'true',
    '/active_output_sweep_restart', $Restart,
    '/fullscreen',
    '/fullscreen_monitor_name', $selected.FriendlyName
)
if ($Tests) { $arguments += @('/active_output_sweep_tests', $Tests) }
& $videoProcessor @arguments
exit $LASTEXITCODE
