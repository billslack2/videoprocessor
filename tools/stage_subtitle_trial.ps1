[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Destination,
 [string]$Seed='E:\codex\subtitle-moving-options-20261003-58a200a',
 [string]$SourceRoot=(Split-Path -Parent $PSScriptRoot),
 [switch]$ReleaseBuildAndTestsPassed
)
$ErrorActionPreference='Stop'
if (-not $ReleaseBuildAndTestsPassed) { throw 'Supply -ReleaseBuildAndTestsPassed only after the full x64 Release build and required tests pass.' }
$dest=[IO.Path]::GetFullPath($Destination).TrimEnd('\')
if (-not $dest.StartsWith('E:\codex\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Choose a new isolated folder under E:\codex.' }
if ((Split-Path -Parent $dest).TrimEnd('\') -ine 'E:\codex') { throw 'Use a new direct child folder of E:\codex for the trial.' }
if (Test-Path -LiteralPath $dest) { throw 'Destination already exists.' }
$seedRoot=(Resolve-Path -LiteralPath $Seed).Path
$release=Join-Path $SourceRoot 'x64\Release'
$map=[ordered]@{
 'VideoProcessor-GUI.exe'='VideoProcessor.exe'
 'vprenderer\VideoProcessorVPRenderer.dll'='vprenderer\VideoProcessorVPRenderer.dll'
 'VideoProcessorConfig.exe'='config\VideoProcessorConfig.exe'
 'VideoProcessorConfigDiscovery.dll'='VideoProcessorConfigDiscovery.dll'
}
foreach($source in $map.Keys) {
 $binary=Join-Path $release $source
 $record=Get-Content -Raw -LiteralPath ($binary+'.runtime.json') | ConvertFrom-Json
 if($record.configuration -ne 'Release' -or $record.architecture -ne 'x64' -or
  (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash -ne $record.sha256) { throw "Invalid Release binary record: $source" }
}
foreach($cfg in @('BOX','MOVE','MOVE-RECTANGLE','MOVE-TRANSPARENT','MOVE-BLEND','MOVE-BLACK','MOVE-DARK-GRAY')) {
 if(-not (Test-Path -LiteralPath (Join-Path $seedRoot ("Subtitle-$cfg.cfg")))) { throw "Seed config missing: $cfg" }
}
New-Item -ItemType Directory -Path $dest | Out-Null
# Copy runtime dependencies only; prior results and playback state stay behind.
foreach($dir in @('config','vprenderer','shaders')) {
 $from=Join-Path $seedRoot $dir
 if(Test-Path -LiteralPath $from) {
  Get-ChildItem -LiteralPath $from -Recurse -File | Where-Object {
   $_.Extension -in @('.dll','.exe','.glsl','.hlsl','.spv','.json','.ps1','.cmd','.txt') -and
   $_.Name -notlike '*.runtime.json' -and $_.Name -notlike '*MANIFEST*' -and
   $_.Name -notlike '*VALIDATION*' -and $_.Name -notlike 'README*'
  } | ForEach-Object {
   $target=Join-Path (Join-Path $dest $dir) $_.FullName.Substring($from.Length).TrimStart('\')
   New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
   Copy-Item -LiteralPath $_.FullName -Destination $target
  }
 }
}
Get-ChildItem -LiteralPath $seedRoot -File | Where-Object {
 $_.Extension -in @('.cfg','.dll') -or $_.Name -eq 'LICENSE.txt'
} | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $dest }
# The generated-background variant starts from the proven dark-gray trial
# profile and changes only its background-mode value below.
Copy-Item -LiteralPath (Join-Path $dest 'Subtitle-MOVE-DARK-GRAY.cfg') `
 -Destination (Join-Path $dest 'Subtitle-MOVE-GENERATED-GRAY.cfg')
foreach($source in $map.Keys) {
 $target=Join-Path $dest $map[$source]
 New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
 Copy-Item -LiteralPath (Join-Path $release $source) -Destination $target -Force
 Copy-Item -LiteralPath ((Join-Path $release $source)+'.runtime.json') -Destination ($target+'.runtime.json') -Force
}
foreach($suffix in @('','.runtime.json')) {
 Copy-Item -LiteralPath (Join-Path $release ('VideoProcessorConfigDiscovery.dll'+$suffix)) -Destination (Join-Path $dest ('config\VideoProcessorConfigDiscovery.dll'+$suffix)) -Force
}
Get-ChildItem -LiteralPath (Join-Path $release 'vprenderer') -Filter '*.dll' -File | ForEach-Object {
 Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $dest 'vprenderer') -Force
}
$licenseRoot=Join-Path $release 'vprenderer\third_party_licenses'
if(Test-Path -LiteralPath $licenseRoot) {
 Copy-Item -LiteralPath $licenseRoot -Destination (Join-Path $dest 'vprenderer') -Recurse -Force
}
foreach($pattern in @('Qt6*.dll','opengl32sw.dll')) {
 Get-ChildItem -LiteralPath $release -Filter $pattern -File | ForEach-Object {
  Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $dest 'config') -Force
 }
}
foreach($dir in @('generic','iconengines','imageformats','networkinformation','platforms','styles','tls')) {
 $from=Join-Path $release $dir
 if(Test-Path -LiteralPath $from) {
  $to=Join-Path $dest ('config\'+$dir)
  New-Item -ItemType Directory -Path $to -Force | Out-Null
  Get-ChildItem -LiteralPath $from -Filter '*.dll' -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $to -Force }
 }
}
$backup=Join-Path $dest ('backups\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
Get-ChildItem -LiteralPath $dest -Filter '*.cfg' -File | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $backup }
function Set-RendererSetting([string]$Text,[string]$Key,[string]$Value) {
 $section=[regex]::Match($Text,'(?ms)^\[vprenderer\][^\r\n]*\r?\n.*?(?=^\[|\z)')
 if(-not $section.Success) { return $Text.TrimEnd()+"`r`n`r`n[vprenderer]`r`n"+$Key+": "+$Value+"`r`n" }
 $pattern='(?m)^('+[regex]::Escape($Key)+'\s*:\s*)[^\r\n#]*(.*)$'
 $body=$section.Value
 if([regex]::IsMatch($body,$pattern)) { $body=[regex]::Replace($body,$pattern,('${1}'+$Value+' ${2}')) }
 else { $body=$body.TrimEnd()+"`r`n"+$Key+": "+$Value+"`r`n" }
 return $Text.Substring(0,$section.Index)+$body+$Text.Substring($section.Index+$section.Length)
}
$modes=[ordered]@{'BOX'='box';'MOVE'='transparent';'MOVE-RECTANGLE'='rectangle';'MOVE-TRANSPARENT'='transparent';'MOVE-BLEND'='blend';'MOVE-BLACK'='black';'MOVE-DARK-GRAY'='dark_gray';'MOVE-GENERATED-GRAY'='generated_gray'}
foreach($label in $modes.Keys) {
 $cfg=Join-Path $dest ("Subtitle-$label.cfg")
 $text=[IO.File]::ReadAllText($cfg)
 foreach($entry in @(@('subtitle_box_padding_sides','60'),@('subtitle_box_padding_top','60'),@('subtitle_box_padding_bottom','20'),@('subtitle_move_inset','0'))) {
  $text=Set-RendererSetting $text $entry[0] $entry[1]
 }
 $text=Set-RendererSetting $text 'subtitle_cut_paste_background' $(if($label -eq 'BOX'){'rectangle'}else{$modes[$label]})
 $text=Set-RendererSetting $text 'subtitle_bbox_test' $(if($label -eq 'BOX'){'true'}else{'false'})
 $text=Set-RendererSetting $text 'subtitle_cut_paste_test' $(if($label -eq 'BOX'){'false'}else{'true'})
 [IO.File]::WriteAllText($cfg,$text,(New-Object Text.UTF8Encoding($false)))
 $launcher=@(
 '@echo off','cd /d "%~dp0"',
 ('echo Subtitle mode: '+$modes[$label]),('echo Config: %~dp0Subtitle-'+$label+'.cfg'),
 ('findstr /B /C:"subtitle_box_padding_" /C:"subtitle_move_inset:" /C:"subtitle_cut_paste_background:" "%~dp0Subtitle-'+$label+'.cfg"'),
 'echo Values are source pixels. Runtime SUBTITLE MODE log reports effective settings.',
 'tasklist /FI "IMAGENAME eq VideoProcessor.exe" /NH 2>nul | find /I "VideoProcessor.exe" >nul',
 'if not errorlevel 1 (',' echo Close the running VideoProcessor first so the capture card is free.',' pause',' exit /b 1',')',
 ('start "Subtitle '+$label+'" /D "%~dp0" "%~dp0VideoProcessor.exe" /config "%~dp0Subtitle-'+$label+'.cfg"')
 ) -join "`r`n"
 [IO.File]::WriteAllText((Join-Path $dest ("START-SUBTITLE-$label.cmd")),$launcher+"`r`n",[Text.Encoding]::ASCII)
}
$readme=@(
 'SUBTITLE CONSISTENCY TRIAL','',
 'Run START-SUBTITLE-BOX.cmd for green boxes. Close VP before switching modes.',
 'Each launcher prints its config and source-pixel padding values.','',
 'START-SUBTITLE-MOVE-RECTANGLE.cmd: copy current rectangle and its picture pixels.',
 'START-SUBTITLE-MOVE-TRANSPARENT.cmd: approximate glyph key over the destination.',
 'START-SUBTITLE-MOVE-BLEND.cmd: the same key with dimmed destination backing.',
 'START-SUBTITLE-MOVE-BLACK.cmd: opaque black destination backing.',
 'START-SUBTITLE-MOVE-DARK-GRAY.cmd: opaque dark gray destination backing.',
 'START-SUBTITLE-MOVE-GENERATED-GRAY.cmd: live destination picture under a rounded semi-transparent gray panel.',
 'START-SUBTITLE-MOVE.cmd: default transparent mode; use MOVE-RECTANGLE for raw patch copy.','',
 'Transparent and blend fill the original subtitle area in the picture using',
 'smooth current-frame side samples. This can smear scenery and cannot recover',
 'hidden detail. The approximate key can lose colored glyphs or dark outlines,',
 'and retain bright picture details. Black and gray use the same glyph key.','',
 'Generated gray keeps live destination pixels under its panel. Clearing the',
 'old in-picture subtitle area uses deterministic row reflection and side',
 'interpolation. Moving scenery can change that fill; hidden scene detail cannot',
 'be recovered from an opaque bar. This mode is experimental.','',
 'Edit [vprenderer] in the selected Subtitle-*.cfg; restart after editing:',
 'subtitle_box_padding_sides: 60','subtitle_box_padding_top: 60',
 'subtitle_box_padding_bottom: 20','subtitle_move_inset: 0',
 'All values are source pixels, integer range 0 through 500. Too much padding',
 'can make a patch too tall to fit; movement then stays disabled for that frame.','',
 'Copied configs were backed up before minimal edits. The runtime SUBTITLE MODE',
 'log reports effective settings after profile rules. This remains experimental.',
 'New validation and hash records are supplied separately.'
) -join "`r`n"
[IO.File]::WriteAllText((Join-Path $dest 'README-SUBTITLE-TRIAL.txt'),$readme,[Text.Encoding]::ASCII)
Write-Output "Staged isolated subtitle trial: $dest"
