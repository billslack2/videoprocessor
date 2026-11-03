$ErrorActionPreference='Stop'
$dir=Join-Path $env:TEMP ('vp-presentation-report-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dir|Out-Null
$rows=@();$id=0
foreach($event in @('initial','correlation','correlation','presentation-change')) {
 $id++;$record=@{schema=1;instance='{12345678-test}';generation=1;input=$id;selected=$id;output=$id;facts_current=$true;tick_ms=$id;utc_ms=$id;timecode="00:00:00.004Z 12345678 G1 O$id S$id";submitted=$true;rendered=$true;prepare_failures=0;reason='fixture'}
 $rows+="2026-11-02 00:00:00 | VP presentation decision: event=$event span=1 "+($record|ConvertTo-Json -Compress)
}
$log=Join-Path $dir 'vp.log';$rows|Set-Content $log
& (Join-Path $PSScriptRoot 'summarize_presentation_log.ps1') -LogPath $log -OutputDirectory (Join-Path $dir 'report') -VideoTimecode '00:00:00.004Z 12345678 G1 O4 S4'
$summary=Get-Content (Join-Path $dir 'report\summary.txt') -Raw
if($summary -notmatch 'decision changes: 2; per-output correlations: 2'){throw 'Per-output records were misclassified as decision changes'}
if(@(Import-Csv (Join-Path $dir 'report\video-match.csv')).Count -ne 1){throw 'Exact timecode correlation failed'}
Write-Output 'PRESENTATION_REPORT_EVENTS_PASS'
