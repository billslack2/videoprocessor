$ErrorActionPreference='Stop'
$vpSource='E:\codex\videoprocessor\pr177-pr203-deploy-20261025'
$vpEvidence='E:\codex\diagnostics\pr177-pr203-deploy-20261025'
$env:QT_QPA_PLATFORM='offscreen'
$env:QT_QPA_FONTDIR='C:\Windows\Fonts'
$env:QT_PLUGIN_PATH=(Join-Path $vpSource 'x64\Release')
$env:QT_SCALE_FACTOR='1'
$env:VP_REQUIRE_CHART_RUNTIME='1'
$env:Path=(Join-Path $vpSource 'x64\Release')+';'+$env:Path
$vpCases=@(
'DPI keyboard and accessibility behavior',
'new action starts unconfigured',
'empty actions show an empty state',
'active profile opens selected after late status and preserves edits',
'shared profile list controller contract',
'custom shader parameters expand into page scroll',
'responsive workspaces preserve edits and fit',
'migration notice only for pending migration',
'no-target startup is quiet',
'Rendering menu duplicate export import preserves ownership',
'Rendering processing ownership save reopen and rename',
'Rendering profile file ownership round trip',
'explicit preset defaults and output matching',
'explicit Auto migration preserves profile choices',
'preset resolution precedes editor section migration',
'renderer promotion migration and cache selection',
'display refresh presentation defaults and round trip',
'Apply OK Cancel contract',
'profile lifecycle through widgets',
'Tone mapping controls round trip',
'Beta tone mapping controls',
'ordinary edits avoid disk validation',
'Off zoom hold round trip',
'subtitle profile tab round trip'
)
$vpResults=@()
foreach($vpCase in $vpCases){
 $vpSafe=$vpCase -replace '[^a-zA-Z0-9-]','-'
 $vpLog=Join-Path $vpEvidence ('gui-'+$vpSafe+'.log')
 $vpProcess=New-Object System.Diagnostics.Process
 $vpProcess.StartInfo.FileName=Join-Path $vpSource 'x64\Release\ConfigQA\VideoProcessorConfigTests.exe'
 $vpProcess.StartInfo.Arguments='--test "'+$vpCase+'"'
 $vpProcess.StartInfo.WorkingDirectory=$vpSource
 $vpProcess.StartInfo.UseShellExecute=$false
 $vpProcess.StartInfo.CreateNoWindow=$true
 $vpProcess.StartInfo.RedirectStandardOutput=$true
 $vpProcess.StartInfo.RedirectStandardError=$true
 if(!$vpProcess.Start()){throw 'Unable to start Config test'}
 $vpOut=$vpProcess.StandardOutput.ReadToEndAsync();$vpErr=$vpProcess.StandardError.ReadToEndAsync()
 if(!$vpProcess.WaitForExit(90000)){$vpProcess.Kill();throw "Config test timed out: $vpCase"}
 $vpExit=$vpProcess.ExitCode
 $vpResult=$vpOut.GetAwaiter().GetResult()+$vpErr.GetAwaiter().GetResult()
 $vpResult | Set-Content -LiteralPath $vpLog -Encoding utf8
 $vpPassed=$vpExit -eq 0 -and $vpResult -match ('PASS '+[regex]::Escape($vpCase))
 $vpResults+=[pscustomobject]@{Test=$vpCase;Exit=$vpExit;Passed=$vpPassed;Result=$vpResult.Trim()}
 if(!$vpPassed){$vpResults | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $vpEvidence 'config-results.json'); throw "Config case failed: $vpCase ($vpExit) $vpResult"}
}
$vpResults | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $vpEvidence 'config-results.json')
"Config tests passed: $($vpResults.Count)"

