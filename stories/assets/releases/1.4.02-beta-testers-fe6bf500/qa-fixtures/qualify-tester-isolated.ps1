# QA ONLY. Generated Setup files are DO-NOT-DISTRIBUTE, never public packages.
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$PayloadRoot,
 [Parameter(Mandatory=$true)][string]$PublicInstallerPath,
 [Parameter(Mandatory=$true)][string]$PortableZipPath,
 [string]$PayloadInclude,
 [string]$IsccPath='E:\codex\tools\inno-6.7.3\app\ISCC.exe'
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
if($root -ine 'E:\codex\videoprocessor\tester-release-20261021-71043e19'){throw 'Dedicated release workspace required.'}
$PayloadRoot=(Resolve-Path -LiteralPath $PayloadRoot).Path.TrimEnd('\')
$PublicInstallerPath=(Resolve-Path -LiteralPath $PublicInstallerPath).Path
$PortableZipPath=(Resolve-Path -LiteralPath $PortableZipPath).Path
$IsccPath=(Resolve-Path -LiteralPath $IsccPath).Path
if(-not $PayloadInclude){$PayloadInclude=Join-Path $root 'artifacts\installer-payload-full.iss'}
$PayloadInclude=(Resolve-Path -LiteralPath $PayloadInclude).Path
if(-not $PayloadRoot.StartsWith($root+'\artifacts\',[StringComparison]::OrdinalIgnoreCase)){throw 'Public staged payload must be within release artifacts.'}
function RealRegistration {
 $h=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
 try {
  $k=$h.OpenSubKey('Software\Microsoft\Windows\CurrentVersion\Uninstall\{42D852F1-70E9-43ED-8739-D61752106D59}_is1')
  if(-not $k){return '<absent>'}
  try {return (@($k.GetValueNames() | Sort-Object | ForEach-Object {[pscustomobject]@{name=$_;kind=[string]$k.GetValueKind($_);value=$k.GetValue($_,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)}}) | ConvertTo-Json -Depth 8 -Compress)} finally {$k.Dispose()}
 } finally {$h.Dispose()}
}
function QtPreferencesHash {
 $h=[Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser,[Microsoft.Win32.RegistryView]::Registry64)
 try {
  $k=$h.OpenSubKey('Software\VideoProcessor\VideoProcessor Configuration')
  if(-not $k){return '<absent>'}
  $records=[System.Collections.Generic.List[object]]::new()
  function ReadPreferenceTree($key,[string]$relative){
   foreach($name in @($key.GetValueNames() | Sort-Object)){$records.Add([pscustomobject]@{key=$relative;name=$name;kind=[string]$key.GetValueKind($name);value=$key.GetValue($name,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)})}
   foreach($name in @($key.GetSubKeyNames() | Sort-Object)){$child=$key.OpenSubKey($name);try{ReadPreferenceTree $child ($relative+'\'+$name)}finally{$child.Dispose()}}
  }
  try{ReadPreferenceTree $k ''; $json=@($records.ToArray()) | ConvertTo-Json -Depth 20 -Compress}finally{$k.Dispose()}
  $sha=[Security.Cryptography.SHA256]::Create()
  try{return [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes([string]$json))).Replace('-','')}finally{$sha.Dispose()}
 }finally{$h.Dispose()}
}
$qtPreferencesBefore=QtPreferencesHash
$registrationBefore=RealRegistration
$originalProcesses=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -in @('VideoProcessor.exe','VideoProcessor-GUI.exe','VideoProcessorConfig.exe')} | Select-Object ProcessId,ExecutablePath,CreationDate)
$protected=@{}
foreach($path in @($PublicInstallerPath,$PortableZipPath)+@($originalProcesses | ForEach-Object {$_.ExecutablePath})){
 if($path -and (Test-Path -LiteralPath $path -PathType Leaf)){$protected[$path]=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
}
$manifest=Get-Content -LiteralPath (Join-Path $PayloadRoot 'INSTALL-MANIFEST.json') -Raw | ConvertFrom-Json
$oldGuid='42D852F1-70E9-43ED-8739-D61752106D59'
if($manifest.applicationId -ne ('VideoProcessor-'+$oldGuid)){throw 'Full public payload required.'}
foreach($entry in $manifest.files){if($entry.policy -eq 'managed'){
 $path=Join-Path $PayloadRoot $entry.path
 $hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
 if($hash -ne $entry.sha256){throw ('Public payload hash mismatch: '+$entry.path)}
 $protected[$path]=$hash
}}
$qa=Join-Path $root ('artifacts\q-'+[guid]::NewGuid().ToString('N').Substring(0,8))
if(Test-Path -LiteralPath $qa){throw 'QA workspace must be new.'}
$qa=[IO.Path]::GetFullPath($qa).TrimEnd('\')
function AssertQaPath([string]$path){
 $full=[IO.Path]::GetFullPath($path)
 if($full -ine $qa -and -not $full.StartsWith($qa+'\',[StringComparison]::OrdinalIgnoreCase)){throw ('Outside QA workspace: '+$full)}
 $part=$full
 while($part){if((Test-Path -LiteralPath $part) -and ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw ('Junction/symlink not allowed: '+$part)};$part=Split-Path -Parent $part}
}
if(-not $qa.StartsWith($root+'\artifacts\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe QA root.'}
AssertQaPath $qa
$newGuid=[guid]::NewGuid().ToString().ToUpperInvariant()
$label='VideoProcessor Tester QA '+$newGuid.Substring(0,8)
$registry='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{'+$newGuid+'}_is1'
if(Test-Path -LiteralPath $registry){throw 'QA registration already exists.'}
$qaTools=Join-Path $qa 'tools'
$null=New-Item -ItemType Directory -Path $qaTools -Force
$payload=Join-Path $qa 'payload'
Copy-Item -LiteralPath $PayloadRoot -Destination $payload -Recurse
$manifest.applicationId=$manifest.applicationId.Replace($oldGuid,$newGuid)
$manifest.uninstallEntryPoint='Uninstall '+$label+'.lnk'
$manifest | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath (Join-Path $payload 'INSTALL-MANIFEST.json') -Encoding UTF8
$helper=Join-Path $payload 'setup\install-support.ps1'
[IO.File]::WriteAllText($helper,([IO.File]::ReadAllText($helper).Replace($oldGuid,$newGuid)),[Text.UTF8Encoding]::new($true))
foreach($entry in $manifest.files){if($entry.policy -eq 'managed' -and (Get-FileHash -LiteralPath (Join-Path $payload $entry.path)).Hash -ne $entry.sha256){throw ('QA managed application bytes changed: '+$entry.path)}}
$iss=[IO.File]::ReadAllText((Join-Path $root 'packaging\installer\VideoProcessor.iss')).Replace($oldGuid,$newGuid)
$iss=$iss.Replace('#define AppDisplay "VideoProcessor"',('#define AppDisplay "'+$label+'"')).Replace('#define AppFolder "VideoProcessor"',('#define AppFolder "'+$label+'"')).Replace('{userprograms}\VideoProcessor\',('{userprograms}\'+$label+'\'))
$qaIss=Join-Path $qa 'qa.iss'
[IO.File]::WriteAllText($qaIss,$iss,[Text.UTF8Encoding]::new($true))
$include=[IO.File]::ReadAllText($PayloadInclude).Replace($PayloadRoot,$payload)
$includePath=Join-Path $qa 'payload.iss'
[IO.File]::WriteAllText($includePath,$include,[Text.UTF8Encoding]::new($true))
foreach($directory in @('packaging\installer','artifacts','images')){$null=New-Item -ItemType Directory -Path (Join-Path $qa $directory) -Force}
Copy-Item -LiteralPath $qaIss -Destination (Join-Path $qa 'packaging\installer\VideoProcessor.iss')
Copy-Item -LiteralPath $includePath -Destination (Join-Path $qa 'artifacts\installer-payload.iss')
Copy-Item -LiteralPath (Join-Path $root 'images\VideoProcessor.ico') -Destination (Join-Path $qa 'images')
$stopGuard='function Stop-QaOwnedProcess([Diagnostics.Process]$p) { if($p.HasExited){return}; $boundary=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd("\")+"\"; if(-not $p.MainModule.FileName.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase)){throw "Refusing to stop a process outside QA workspace."}; $p.Kill(); if(-not $p.WaitForExit(5000)){throw "QA Config exit timed out."} }'
foreach($name in @('test_installer_e2e.ps1','test_installer_zip_adoption.ps1','test_installer_failure.ps1')){
 $text=[IO.File]::ReadAllText((Join-Path $root ('tools\'+$name))).Replace($oldGuid,$newGuid).Replace('Uninstall VideoProcessor.lnk',('Uninstall '+$label+'.lnk'))
 $text=$text.Replace("@('--background','--config',","@('--background','--offline','--config',").Replace('Stop-Process -Id $configProcess.Id','Stop-QaOwnedProcess $configProcess')
 # Scratch names alone are shortened. Original production Setup/package bytes
 # and test assertions stay unchanged; all fixture roots stay in QA artifacts.
 foreach($pair in @(@('installer-e2e-','e-'),@('zip-adoption-','z-'),@('installer-failure-','f-'))){$text=$text.Replace($pair[0],$pair[1])}
 $text=$text.Replace("[guid]::NewGuid().ToString('N')","[guid]::NewGuid().ToString('N').Substring(0,8)")
 $text=$text.Replace('Custom VP space','VP QA space').Replace('Existing ZIP VP','ZIP VP').Replace('failure-target','VP fail')

 if($name -eq 'test_installer_e2e.ps1'){
  $needle='function Assert([bool]$Condition,[string]$Message){'
  if(-not $text.Contains($needle)){throw 'E2E fixture scope insertion point changed.'}
  $text=$text.Replace($needle,$stopGuard+"`r`n"+$needle)
 }
 [IO.File]::WriteAllText((Join-Path $qaTools $name),$text,[Text.UTF8Encoding]::new($true))
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$portable=Join-Path $qa 'portable'
$zip=[IO.Compression.ZipFile]::OpenRead($PortableZipPath)
try{foreach($entry in $zip.Entries){if([IO.Path]::IsPathRooted($entry.FullName)){throw 'Absolute ZIP entry.'};AssertQaPath ([IO.Path]::Combine($portable,$entry.FullName.Replace('/','\')))}}finally{$zip.Dispose()}
[IO.Compression.ZipFile]::ExtractToDirectory($PortableZipPath,$portable)
$portableManifest=Join-Path $portable 'INSTALL-MANIFEST.json'
$inventory=Get-Content -LiteralPath $portableManifest -Raw | ConvertFrom-Json
if($inventory.applicationId -ne ('VideoProcessor-'+$oldGuid)){throw 'Expected full portable product.'}
$inventory.applicationId=$inventory.applicationId.Replace($oldGuid,$newGuid)
$inventory.uninstallEntryPoint='Uninstall '+$label+'.lnk'
$inventory | ConvertTo-Json -Depth 15 | Set-Content -LiteralPath $portableManifest -Encoding UTF8
$projectedMaxManagedPath=0
foreach($fixture in @('artifacts\e-12345678\VP QA space','artifacts\z-12345678\ZIP VP','artifacts\f-12345678\VP fail')){
 foreach($entry in $manifest.files){
  $dest=Join-Path (Join-Path $qa $fixture) $entry.path
  AssertQaPath $dest
  $projectedMaxManagedPath=[Math]::Max($projectedMaxManagedPath,$dest.Length)
 }
}
if($projectedMaxManagedPath -gt 200){throw ('QA scratch destination exceeds 200 characters: '+$projectedMaxManagedPath)}
$numeric=($manifest.coreVersion -split '-',2)[0]
$parts=@($numeric -split '\.')
if($parts.Count -ne 3){throw 'Expected three-component core version for QA compiler.'}
$fileVersion=$numeric+'.0'
& $IsccPath "/DPayloadRoot=$payload" "/DPayloadInclude=$includePath" "/DOutputRoot=$qa" "/DCoreVersion=$($manifest.coreVersion)" "/DBuildCommit=$($manifest.sourceCommit.Substring(0,12))" '/DInstallerBaseName=DO-NOT-DISTRIBUTE-Tester-QA' "/DFileVersion=$fileVersion" "/DSetupIcon=$root\images\VideoProcessor.ico" $qaIss *> (Join-Path $qa 'compile.log')
if($LASTEXITCODE -ne 0){throw ('QA compilation failed: '+$qa)}
$installer=Join-Path $qa 'DO-NOT-DISTRIBUTE-Tester-QA.exe'
$ps=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
$checks=@(
 @{name='lifecycle';file='test_installer_e2e.ps1';args=@('-InstallerPath',$installer,'-PayloadRoot',$payload)},
 @{name='zip-adoption';file='test_installer_zip_adoption.ps1';args=@('-InstallerPath',$installer,'-PortableRoot',$portable,'-PayloadRoot',$payload)},
 @{name='failure';file='test_installer_failure.ps1';args=@('-PayloadRoot',$payload,'-IsccPath',$IsccPath)}
)
try{
 foreach($check in $checks){
  $test=Join-Path $qaTools $check.file
  & $ps -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $test @($check.args) *> (Join-Path $qa ($check.name+'.log'))
  if($LASTEXITCODE -ne 0){throw ('QA '+$check.name+' failed; preserve evidence at '+$qa)}
  if(Test-Path -LiteralPath $registry){throw ('QA '+$check.name+' left its disposable registration.')}
 }
}finally{
 $mismatches=@()
 foreach($path in $protected.Keys){if(-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $protected[$path]){$mismatches+=$path}}
 $sameRegistration=(RealRegistration) -ceq $registrationBefore
 $qtPreferencesAfter=QtPreferencesHash
 $qtPreferencesUnchanged=$qtPreferencesAfter -ceq $qtPreferencesBefore
 $processObservations=@($originalProcesses | ForEach-Object {$prior=$_;$current=Get-CimInstance Win32_Process -Filter ('ProcessId='+$prior.ProcessId);[pscustomobject]@{pid=$prior.ProcessId;path=$prior.ExecutablePath;stillSameProcess=[bool]($current -and $current.CreationDate -eq $prior.CreationDate -and $current.ExecutablePath -eq $prior.ExecutablePath)}})
 [pscustomobject]@{qaRoot=$qa;qaIdentity=$newGuid;projectedMaxManagedPath=$projectedMaxManagedPath;sourceCommit=$manifest.sourceCommit;publicSetupSha256=$protected[$PublicInstallerPath];portableZipSha256=$protected[$PortableZipPath];realRegistrationUnchanged=$sameRegistration;qtPreferencesBeforeSha256=$qtPreferencesBefore;qtPreferencesAfterSha256=$qtPreferencesAfter;qtPreferencesUnchanged=$qtPreferencesUnchanged;protectedHashMismatches=$mismatches;originalProcessObservations=$processObservations;coverageLimit='Same-user isolated GUID on developer Windows. Clean VM, interactive persistence and cross-version rollback not qualified. QA Config is offline but shares per-user Qt preferences.'} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $qa 'isolation-receipt.json') -Encoding UTF8
 if(-not $qtPreferencesUnchanged){Write-Warning 'Shared Qt preferences changed during QA; no restoration attempted because the real editor may be editing concurrently. Review this evidence before claiming preference isolation.'}
 if(-not $sameRegistration -or $mismatches.Count){throw ('Protected real registration or public payload changed; inspect '+$qa)}
}
Write-Output ('Isolated lifecycle/adoption/failure passed. Evidence: '+$qa)