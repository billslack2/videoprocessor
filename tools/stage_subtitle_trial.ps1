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
# Preserve an existing generated-background profile, including its user style.
# Only seeds predating that mode need a profile cloned from dark gray.
if(-not (Test-Path -LiteralPath (Join-Path $dest 'Subtitle-MOVE-GENERATED-GRAY.cfg'))) {
 Copy-Item -LiteralPath (Join-Path $dest 'Subtitle-MOVE-DARK-GRAY.cfg') `
  -Destination (Join-Path $dest 'Subtitle-MOVE-GENERATED-GRAY.cfg')
}
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
function Ensure-RendererSetting([string]$Text,[string]$Key,[string]$Value) {
 $section=[regex]::Match($Text,'(?ms)^\[vprenderer\][^\r\n]*\r?\n.*?(?=^\[|\z)')
 $pattern='(?m)^'+[regex]::Escape($Key)+'\s*:'
 if($section.Success -and [regex]::IsMatch($section.Value,$pattern)) { return $Text }
 return Set-RendererSetting $Text $Key $Value
}
$modes=[ordered]@{'BOX'='box';'MOVE'='transparent';'MOVE-RECTANGLE'='rectangle';'MOVE-TRANSPARENT'='transparent';'MOVE-BLEND'='blend';'MOVE-BLACK'='black';'MOVE-DARK-GRAY'='dark_gray';'MOVE-GENERATED-GRAY'='generated_gray'}
foreach($label in $modes.Keys) {
 $cfg=Join-Path $dest ("Subtitle-$label.cfg")
 $text=[IO.File]::ReadAllText($cfg)
 $shortcutSection=[regex]::Match($text,'(?ms)^\[shortcuts\][^\r\n]*\r?\n.*?(?=^\[|\z)')
 if(-not $shortcutSection.Success) { $text += "`r`n[shortcuts]`r`nsubtitle_toggle: Ctrl+Shift+T`r`n" }
 elseif(-not [regex]::IsMatch($shortcutSection.Value,'(?m)^subtitle_toggle\s*:')) {
  $at=$shortcutSection.Index+$shortcutSection.Length
  $text=$text.Insert($at,"subtitle_toggle: Ctrl+Shift+T`r`n")
 }
 foreach($entry in @(@('subtitle_box_padding_sides','60'),@('subtitle_box_padding_top','40'),@('subtitle_box_padding_bottom','21'),@('subtitle_move_inset','0'),@('subtitle_hold_ms','250'))) {
  $text=Ensure-RendererSetting $text $entry[0] $entry[1]
 }
 if($label -eq 'MOVE-GENERATED-GRAY') {
  foreach($entry in @(@('subtitle_generated_gray_color','040404'),@('subtitle_generated_gray_opacity','0.85'),@('subtitle_generated_gray_blur_px','3'),@('subtitle_generated_gray_max_luminance','0.16'),@('subtitle_generated_gray_border_color','000000'),@('subtitle_generated_gray_border_opacity','0.65'),@('subtitle_generated_gray_border_width','0'))) {
   $text=Ensure-RendererSetting $text $entry[0] $entry[1]
  }
 }
 $text=Set-RendererSetting $text 'subtitle_cut_paste_background' $(if($label -eq 'BOX'){'rectangle'}else{$modes[$label]})
 $text=Set-RendererSetting $text 'subtitle_bbox_test' $(if($label -eq 'BOX'){'true'}else{'false'})
 $text=Set-RendererSetting $text 'subtitle_cut_paste_test' $(if($label -eq 'BOX'){'false'}else{'true'})
 [IO.File]::WriteAllText($cfg,$text,(New-Object Text.UTF8Encoding($false)))
 $launcher=@(
 '@echo off','cd /d "%~dp0"',
 ('echo Profile: '+$modes[$label]+' - starts in normal configured playback'),('echo Config: %~dp0Subtitle-'+$label+'.cfg'),
 ('findstr /B /C:"subtitle_box_padding_" /C:"subtitle_move_inset:" /C:"subtitle_hold_ms:" /C:"subtitle_toggle:" /C:"subtitle_cut_paste_background:" /C:"subtitle_generated_gray_" "%~dp0Subtitle-'+$label+'.cfg"'),
 'echo Padding/border width use source pixels; opacity ranges from 0 to 1; colors are RRGGBB.',
 'echo Runtime SUBTITLE MODE log reports effective settings after profile rules.',
 'echo Ctrl+Shift+T: normal, generated gray, black, normal. HDMI resync restores normal.',
 'tasklist /FI "IMAGENAME eq VideoProcessor.exe" /NH 2>nul | find /I "VideoProcessor.exe" >nul',
 'if not errorlevel 1 (',' echo Close the running VideoProcessor first so the capture card is free.',' pause',' exit /b 1',')',
 ('start "Subtitle '+$label+'" /D "%~dp0" "%~dp0VideoProcessor.exe" /config "%~dp0Subtitle-'+$label+'.cfg"')
 ) -join "`r`n"
 [IO.File]::WriteAllText((Join-Path $dest ("START-SUBTITLE-$label.cmd")),$launcher+"`r`n",[Text.Encoding]::ASCII)
}
Copy-Item -LiteralPath (Join-Path $dest 'START-SUBTITLE-MOVE-GENERATED-GRAY.cmd') -Destination (Join-Path $dest 'START-SUBTITLE.cmd')
$readme=@(
 'SUBTITLE CONSISTENCY TRIAL','',
 'All launchers start with normal configured picture placement. Use Ctrl+Shift+T to enable moving subtitles.',
 'Each launcher prints its config and source-pixel padding values.','',
 'Recommended launcher: START-SUBTITLE.cmd (uses Subtitle-MOVE-GENERATED-GRAY.cfg).',
 'The eight legacy launchers remain available and retain their own cfg files.',
 'Their names identify a settings profile; all now start in normal playback.',
 'Old subtitle experiment flags no longer override normal startup/resync behavior.','',
 'During playback, press Ctrl+Shift+T to cycle normal -> generated gray -> black -> normal. HDMI resync restores normal configured playback.','',
 'Black and generated-gray backgrounds extend six extra source pixels per side.',
 'This affects only their displayed background; text capture and position stay unchanged.','',
 'Generated gray continues nearby picture detail into the old subtitle card,',
 'using a reflected patch with a side feather. Directional scenery can look',
 'vertically reversed; the smooth highlight shoulder dims bright scenes while',
 'preserving more local texture than a hard luminance cap. Hidden scene detail',
 'cannot be recovered from a single frame or opaque bar. This mode is experimental.','',
 'Edit [vprenderer] in the selected Subtitle-*.cfg; restart after editing:',
 'Existing seed values/comments are preserved. Defaults below apply only to missing settings:',
 'subtitle_box_padding_sides: 60','subtitle_box_padding_top: 40',
 'subtitle_box_padding_bottom: 21','subtitle_move_inset: 0',
 'Padding and inset use source pixels, integer range 0 through 500. Too much padding',
 'can make a patch too tall to fit; movement then stays disabled for that frame.','',
 'Generated-gray-only styling, in Subtitle-MOVE-GENERATED-GRAY.cfg:',
 'subtitle_generated_gray_color: 040404','subtitle_generated_gray_opacity: 0.85',
 'subtitle_generated_gray_blur_px: 3 (0 disables; maximum 30 source pixels)',
 'subtitle_generated_gray_max_luminance: 0.16',
 'subtitle_generated_gray_border_color: 000000','subtitle_generated_gray_border_opacity: 0.65',
 'subtitle_generated_gray_border_width: 0','',
 'Colors use six-digit sRGB RRGGBB hex without #. Opacity is 0.0 to 1.0;',
 'luminance knee is linear reference-white Y from 0.01 to 1.0; higher values',
 'retain more highlight texture before the smooth shoulder compresses it. Border width',
 'is 0 to 8 source pixels and 0 disables the border.','',
 'Copied configs were backed up before minimal edits. The runtime SUBTITLE MODE',
 'log reports effective settings after profile rules. This remains experimental.',
 'New validation and hash records are supplied separately.'
) -join "`r`n"
[IO.File]::WriteAllText((Join-Path $dest 'README-SUBTITLE-TRIAL.txt'),$readme,[Text.Encoding]::ASCII)
Write-Output "Staged isolated subtitle trial: $dest"
