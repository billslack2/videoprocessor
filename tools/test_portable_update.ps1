[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$IsccPath)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$qa=Join-Path $root ('artifacts\portable-update-e2e-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $qa -Force
$fullGuid=[guid]::NewGuid().ToString().ToUpperInvariant()
$configGuid=[guid]::NewGuid().ToString().ToUpperInvariant()
$label='VP Portable QA '+[guid]::NewGuid().ToString('N')
# Isolate every installer identity and shortcut from real installations.
$scriptText=Get-Content (Join-Path $root 'packaging\installer\VideoProcessor.iss') -Raw
$helper=Get-Content (Join-Path $root 'packaging\installer\install-support.ps1') -Raw
foreach($old in @('42D852F1-70E9-43ED-8739-D61752106D59','BA15DBE8-210F-42AA-AE86-B4628395E77F')) {
 $new=if($old.StartsWith('42D')){$fullGuid}else{$configGuid}
 $scriptText=$scriptText.Replace($old,$new);$helper=$helper.Replace($old,$new)
}
$scriptText=$scriptText.Replace('"VideoProcessor Config"',('"'+$label+' Config"')).Replace('"VideoProcessor"',('"'+$label+'"')).Replace('{userprograms}\VideoProcessor\',('{userprograms}\'+$label+'\'))
$iss=Join-Path $qa 'qa.iss';$scriptText | Set-Content $iss -Encoding UTF8
foreach($flavor in @('full','config')) {
 $work=Join-Path $qa $flavor
 $payload=Join-Path $work 'payload';$install=Join-Path $work 'portable'
 $null=New-Item -ItemType Directory -Path "$payload\setup","$payload\config",$install -Force
 $helper | Set-Content "$payload\setup\install-support.ps1" -Encoding UTF8
 $relative=if($flavor -eq 'full'){'VideoProcessor.exe'}else{'config/VideoProcessorConfig.exe'}
 $file=Join-Path $install $relative
 $null=New-Item -ItemType Directory -Path (Split-Path $file -Parent) -Force
 [IO.File]::WriteAllText($file,'old fixture binary')
 $appId=if($flavor -eq 'full'){'VideoProcessor-'+$fullGuid}else{'VideoProcessorConfig-'+$configGuid}
 $inventory=[ordered]@{schemaVersion=1;build='qa';applicationId=$appId;files=@(@{path=$relative;policy='managed';sha256=(Get-FileHash $file).Hash})}
 $inventory | ConvertTo-Json -Depth 5 | Set-Content "$install\INSTALL-MANIFEST.json" -Encoding UTF8
 [IO.File]::WriteAllText("$install\VideoProcessor.cfg",'# preserve my settings')
 $source=Join-Path $payload $relative
 [IO.File]::WriteAllText($source,'new fixture binary')
 $inventory.files[0].sha256=(Get-FileHash $source).Hash
 $inventory | ConvertTo-Json -Depth 5 | Set-Content "$payload\INSTALL-MANIFEST.json" -Encoding UTF8
 $dest=if($flavor -eq 'full'){'{app}'}else{'{app}\config'}
 ('Source: "'+$source+'"; DestDir: "'+$dest+'"; Flags: ignoreversion') | Set-Content "$work\payload.iss" -Encoding UTF8
 $extra=@();if($flavor -eq 'config'){$extra+='/DConfigOnly'}
 & $IsccPath @extra "/DPayloadRoot=$payload" "/DPayloadInclude=$work\payload.iss" "/DOutputRoot=$work" '/DCoreVersion=1.0.0' '/DBuildCommit=qa' '/DInstallerBaseName=qa' '/DFileVersion=1.0.0.0' "/DSetupIcon=$root\images\VideoProcessor.ico" $iss > "$work\compile.log" 2>&1
 if($LASTEXITCODE -ne 0){Get-Content "$work\compile.log" | Select-Object -Last 12;throw 'QA installer compile failed'}
 $process=Start-Process -FilePath "$work\qa.exe" -ArgumentList @('/SP-','/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/PORTABLEUPDATE=1',('/DIR="'+$install+'"'),('/LOG="'+$work+'\setup.log"')) -WindowStyle Hidden -Wait -PassThru
 if($process.ExitCode -ne 0){throw "Portable setup failed: $($process.ExitCode), see $work\setup.log"}
 if([IO.File]::ReadAllText($file) -ne 'new fixture binary'){throw 'Portable binary not updated'}
 if([IO.File]::ReadAllText("$install\VideoProcessor.cfg") -ne '# preserve my settings'){throw 'Settings changed'}
 if(@(Get-ChildItem $install -Filter 'unins*').Count -or @(Get-ChildItem $install -Filter '*.lnk').Count){throw 'Portable update created uninstaller/shortcut'}
 $guid=if($flavor -eq 'full'){$fullGuid}else{$configGuid}
 if(Test-Path ('HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{'+$guid+'}_is1')){throw 'Portable update registered installation'}
 $programs=[Environment]::GetFolderPath('Programs')
 if(Test-Path (Join-Path $programs $label)){throw 'Portable update created Start menu shortcut'}
 if(Test-Path (Join-Path $programs ($label+' Config'))){throw 'Portable update created Config Start menu shortcut'}
 if((Get-FileHash "$install\INSTALL-MANIFEST.json").Hash -ne (Get-FileHash "$payload\INSTALL-MANIFEST.json").Hash){throw 'Inventory differs from signed package inventory'}
 Write-Host "PASS $flavor portable setup: in-place binary update, preserved settings, exact inventory, no registration/shortcuts/uninstaller"
}
Write-Host "Fixtures and logs: $qa"
