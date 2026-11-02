[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$LogPath,[Parameter(Mandatory=$true)][string]$OutputDirectory,[string]$ScreenshotMetadataPath)
$ErrorActionPreference='Stop'
$LogPath=[IO.Path]::GetFullPath($LogPath)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Choose a new report directory; previous reports are preserved.'}
$rows=[Collections.Generic.List[object]]::new();$invalid=0;$legacy=[Collections.Generic.List[object]]::new();$legacyKey='';$loss=@{}
$gateNames=@('composition-failed','preparation-missing','generation-mismatch','source-mismatch','viewport-mismatch','mode-mismatch','policy-mismatch','continuity-mismatch','geometry-missing','geometry-generation-mismatch','scene-verification-only','non-native-origin','geometry-untrusted','retention-generation-mismatch','retention-source-mismatch')
foreach($line in [IO.File]::ReadLines($LogPath)) {
 if($line -match 'VP async log telemetry:.*transport=(\S+).*dropped_full=(\d+).*truncated=(\d+)'){$loss[$Matches[1]]=@([long]$Matches[2],[long]$Matches[3])}
 if($line -match '^(.*?) \| VP presentation decision: event=(\S+) span=(\d+) (\{.*\})$') {
  $wall=$Matches[1];$event=$Matches[2];$span=[long]$Matches[3];$json=$Matches[4]
  try {$d=$json | ConvertFrom-Json;if($d.schema -ne 1){$invalid++;continue}}catch{$invalid++;continue}
  $failed=@();for($i=0;$i -lt $gateNames.Count;$i++){if([uint32]$d.prepare_failures -band (1 -shl $i)){$failed+=$gateNames[$i]}}
  $rows.Add([pscustomobject]@{Time=$wall;TickMs=$d.tick_ms;Instance=$d.instance;Generation=$d.generation;Input=$d.input;Selected=$d.selected;Output=$d.output;Event=$event;Span=$span;FactsCurrent=$d.facts_current;Native=($d.native -join ',');Measured=($d.measured -join ',');ActualSource=($d.actual_source -join ',');ActualTarget=($d.actual_target -join ',');Mode=$d.mode;Composed=$d.composed;CaptionCurrent=$d.caption_current;ScanReason=$d.scan_reason;OwnershipReason=$d.ownership_reason;PreparationFailures=($failed -join ',');DecisionReason=$d.reason;Rendered=$d.rendered;Submitted=$d.submitted;Record=$json})
 } elseif($line -match '^(.*?) \| Alpha native presentation commit: generation=(\d+) selected=(\d+).*native_rect=([^ ]+).*actual=([^ ]+).*reason=(.*)$') {
  $key=$Matches[2]+'|'+$Matches[4]+'|'+$Matches[5]
  if($key -ne $legacyKey){$legacy.Add([pscustomobject]@{Time=$Matches[1];Generation=$Matches[2];Selected=$Matches[3];Native=$Matches[4];ActualSource=$Matches[5];DecisionReason=$Matches[6];Event='legacy-geometry-change';FactsCurrent='unknown'});$legacyKey=$key}
 }
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$mode='structured';$reportRows=$rows
if(!$rows.Count){$mode='legacy-limited';$reportRows=$legacy}
$reportRows | Select-Object * -ExcludeProperty Record | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'timeline.csv') -NoTypeInformation
if($rows.Count){$rows.Record | Set-Content -LiteralPath (Join-Path $OutputDirectory 'decisions.jsonl')}
$changes=@($reportRows | Where-Object {$_.Event -ne 'heartbeat'})
$summary=[Collections.Generic.List[string]]::new()
$summary.Add("Presentation report: $mode");$summary.Add("Source: $LogPath");$summary.Add("Records: $($reportRows.Count); changes: $($changes.Count); malformed/unsupported records: $invalid")
if($mode -eq 'legacy-limited'){$summary.Add('Legacy log: geometry changes only. Missing caption/preparation facts are UNKNOWN. They cannot be reconstructed from these records.')}
$sumDropped=0L;$sumTruncated=0L;foreach($v in $loss.Values){$sumDropped+=$v[0];$sumTruncated+=$v[1]}
$summary.Add("Last reported cumulative async losses by transport: dropped=$sumDropped truncated=$sumTruncated. Missing loss reports do not prove zero loss.")
$summary.Add('Latest changes (see timeline.csv for complete chronology):')
foreach($c in ($changes | Select-Object -Last 30)){$summary.Add("$($c.Time) source=$($c.Selected) native=$($c.Native) displayed=$($c.ActualSource) composed=$($c.Composed) scan=$($c.ScanReason) preparation=$($c.PreparationFailures) reason=$($c.DecisionReason)")}
if($ScreenshotMetadataPath) {
 $shot=Get-Content -LiteralPath $ScreenshotMetadataPath -Raw | ConvertFrom-Json
 if($shot.presentation_decision) {
  $s=$shot.presentation_decision
  $exact=@($rows | Where-Object {$_.Instance -eq $s.instance -and $_.Output -eq $s.output -and $_.Generation -eq $s.generation})
  $summary.Add("Screenshot: capture=$($shot.capture_id) instance=$($s.instance) output=$($s.output) selected=$($s.selected) exact_log_records=$($exact.Count). Screenshot stage is before Present; its submitted flag does not report eventual Present success.")
  $s | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'screenshot-decision.json')
 }else{$summary.Add('Screenshot sidecar has no presentation_decision. Exact pipeline correlation unavailable.')}
}
$summary | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.txt')
$summary | Select-Object -First 5
