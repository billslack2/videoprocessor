[CmdletBinding()]
param([Parameter(Mandatory)][string]$Checkout,[Parameter(Mandatory)][string]$PreviousCheckout)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$qa=Join-Path $Checkout ('artifacts\full-installer-qa-'+[guid]::NewGuid().ToString('N'))
$qaGuid=[guid]::NewGuid().ToString().ToUpperInvariant()
$label='VideoProcessor RC6 DeckLink QA '+$qaGuid.Substring(0,8)
$originalGuid='42D852F1-70E9-43ED-8739-D61752106D59'
$null=New-Item -ItemType Directory -Path (Join-Path $qa 'tools') -Force
$test=(Get-Content (Join-Path $Checkout 'tools\test_installer_e2e.ps1') -Raw).Replace($originalGuid,$qaGuid)
[IO.File]::WriteAllText((Join-Path $qa 'tools\test_installer_e2e.ps1'),$test)
$installs=@{}
foreach($variant in @('current','previous')) {
 $source=if($variant -eq 'current'){$Checkout}else{$PreviousCheckout}
 $work=Join-Path $qa $variant
 $null=New-Item -ItemType Directory -Path $work
 $originalPayload=Join-Path $source 'artifacts\release\VideoProcessor'
 Copy-Item -LiteralPath $originalPayload -Destination (Join-Path $work 'payload') -Recurse
 $payload=Join-Path $work 'payload'
 $manifestPath=Join-Path $payload 'INSTALL-MANIFEST.json'
 $manifest=Get-Content $manifestPath -Raw | ConvertFrom-Json
 foreach($entry in $manifest.files) {
  if((Get-FileHash -LiteralPath (Join-Path $payload $entry.path)).Hash -ne $entry.sha256){throw 'Copied QA payload hash mismatch'}
 }
 $manifest.applicationId='VideoProcessor-'+$qaGuid
 $manifest.uninstallEntryPoint='Uninstall '+$label+'.lnk'
 $helperPath=Join-Path $payload 'setup\install-support.ps1'
 $helper=(Get-Content -LiteralPath $helperPath -Raw).Replace($originalGuid,$qaGuid)
 [IO.File]::WriteAllText($helperPath,$helper)
 foreach($entry in $manifest.cleanupFiles) {
  if($entry.path -eq 'setup/install-support.ps1'){$entry.sha256=(Get-FileHash -LiteralPath $helperPath).Hash}
 }
 $manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding utf8
 $iss=(Get-Content (Join-Path $source 'packaging\installer\VideoProcessor.iss') -Raw).Replace($originalGuid,$qaGuid).Replace('#define AppDisplay "VideoProcessor"',('#define AppDisplay "'+$label+'"')).Replace('#define AppFolder "VideoProcessor"',('#define AppFolder "'+$label+'"')).Replace('{userprograms}\VideoProcessor\',('{userprograms}\'+$label+'\'))
 [IO.File]::WriteAllText((Join-Path $work 'qa.iss'),$iss)
 $include=(Get-Content (Join-Path $source 'artifacts\installer-payload-full.iss') -Raw).Replace($originalPayload,$payload)
 [IO.File]::WriteAllText((Join-Path $work 'payload.iss'),$include)
 $name='DO-NOT-DISTRIBUTE-RC6-DECKLINK-QA-'+$variant
 & 'E:\codex\tools\inno-6.7.3\app\ISCC.exe' "/DPayloadRoot=$payload" "/DPayloadInclude=$work\payload.iss" "/DOutputRoot=$work" "/DCoreVersion=$($manifest.coreVersion)" "/DBuildCommit=$($manifest.sourceCommit.Substring(0,12))" "/DInstallerBaseName=$name" '/DFileVersion=1.4.0.0' "/DSetupIcon=$Checkout\images\VideoProcessor.ico" (Join-Path $work 'qa.iss') *> (Join-Path $work 'compile.log')
 if($LASTEXITCODE -ne 0){throw "QA compilation failed: $work"}
 $installs[$variant]=Join-Path $work ($name+'.exe')
}
& (Join-Path $qa 'tools\test_installer_e2e.ps1') -InstallerPath $installs.current -PayloadRoot (Join-Path $qa 'current\payload') -PreviousInstallerPath $installs.previous -PreviousManifestPath (Join-Path $qa 'previous\payload\INSTALL-MANIFEST.json') *> (Join-Path $qa 'lifecycle.log')
Write-Host "Installer lifecycle passed: $qa"
