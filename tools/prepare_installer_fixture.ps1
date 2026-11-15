[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$PayloadRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$IsccPath,
    [ValidateSet('full','config')][string]$Flavor='full',[switch]$NoCompile,[string]$DisplayLabel
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
$PayloadRoot=(Resolve-Path -LiteralPath $PayloadRoot).Path
$IsccPath=(Resolve-Path -LiteralPath $IsccPath).Path
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$null=New-Item -ItemType Directory -Path $OutputDirectory -Force
$manifest=Get-Content -LiteralPath (Join-Path $PayloadRoot 'INSTALL-MANIFEST.json') -Raw | ConvertFrom-Json
if($manifest.schemaVersion -ne 1){throw 'Fixture source manifest is unsupported.'}
$fullId='VideoProcessor-0B37C49A-79D2-4D5D-9A7E-C99B31A2F001'
$configId='VideoProcessorConfig-7AD19C54-1F18-4227-8AF1-C99B31A2F002'
if($Flavor -eq 'full'){
 if($manifest.applicationId -notlike 'VideoProcessor-*'){throw 'Full installer fixture requires a full VP payload.'}
 $manifest.applicationId=$fullId
}else{
 if($manifest.applicationId -notlike 'VideoProcessorConfig-*'){throw 'Config installer fixture requires a Config payload.'}
 $manifest.applicationId=$configId
}
$manifestPath=Join-Path $OutputDirectory 'INSTALL-MANIFEST.json'
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
$helper=Join-Path $OutputDirectory 'install-support.exe'
& (Join-Path $PSScriptRoot 'build_installer_support.ps1') -FixtureIdentity -OutputPath $helper
if($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $helper)){throw 'Fixture installer helper build failed.'}

$includePath=Join-Path $OutputDirectory 'payload.iss'
$lines=@(foreach($entry in $manifest.files){
 $relative=$entry.path.Replace('/','\')
 $directory=Split-Path -Parent $relative
 $dest=if($directory){"{app}\$directory"}else{'{app}'}
 $flags=if($entry.policy -eq 'seed'){'onlyifdoesntexist uninsneveruninstall'}else{'ignoreversion'}
 'Source: "'+(Join-Path $PayloadRoot $relative)+'"; DestDir: "'+$dest+'"; Flags: '+$flags
})
$lines | Set-Content -LiteralPath $includePath -Encoding UTF8
$iss=[IO.File]::ReadAllText((Join-Path $root 'packaging\installer\VideoProcessor.iss'))
$iss=$iss.Replace('42D852F1-70E9-43ED-8739-D61752106D59','0B37C49A-79D2-4D5D-9A7E-C99B31A2F001')
$iss=$iss.Replace('BA15DBE8-210F-42AA-AE86-B4628395E77F','7AD19C54-1F18-4227-8AF1-C99B31A2F002')
$iss=$iss.Replace('Source: "{#PayloadRoot}\setup\install-support.exe"; Flags: dontcopy',
    'Source: "'+$helper+'"; Flags: dontcopy')
$iss=$iss.Replace('Source: "{#PayloadRoot}\INSTALL-MANIFEST.json"; Flags: dontcopy',
    'Source: "'+$manifestPath+'"; Flags: dontcopy')
$iss=$iss.Replace('Source: "{#PayloadRoot}\INSTALL-MANIFEST.json"; DestDir: "{tmp}"; DestName: "verification-marker.json"; Flags: ignoreversion deleteafterinstall; AfterInstall: VerifyPayload',
    'Source: "'+$manifestPath+'"; DestDir: "{tmp}"; DestName: "verification-marker.json"; Flags: ignoreversion deleteafterinstall; AfterInstall: VerifyPayload')
$label=if($DisplayLabel){$DisplayLabel}else{'VideoProcessor QA '+[guid]::NewGuid().ToString('N').Substring(0,8)}
if($Flavor -eq 'full'){$manifest.uninstallEntryPoint='Uninstall '+$label+'.lnk'}
else{$manifest.uninstallEntryPoint='Uninstall '+$label+' Config.lnk'}
$supportEvidence=[ordered]@{
 path='setup/install-support.exe';architecture='x64';configuration='Release'
 targetFramework='.NET Framework 4.0';sha256=(Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash
}
$manifest | Add-Member -NotePropertyName installerSupport -NotePropertyValue $supportEvidence -Force
$manifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
if($Flavor -eq 'full'){
 $iss=$iss.Replace('AppDisplay "VideoProcessor"','AppDisplay "'+$label+'"')
 $iss=$iss.Replace('VideoProcessor Setup','VideoProcessor QA Setup')
 $iss=$iss.Replace('{userprograms}\VideoProcessor\','{userprograms}\'+$label+'\')
}else{
 $iss=$iss.Replace('AppDisplay "VideoProcessor Config"','AppDisplay "'+$label+' Config"')
 $iss=$iss.Replace('{userprograms}\VideoProcessor Config\','{userprograms}\'+$label+' Config\')
}
$issPath=Join-Path $OutputDirectory 'fixture.iss'
[IO.File]::WriteAllText($issPath,$iss,[Text.UTF8Encoding]::new($true))
$numbers=($manifest.coreVersion -split '-',2)[0].Split('.')
$fileVersion=if($numbers.Count -eq 3){($numbers -join '.')+'.0'}else{'1.0.0.0'}
$baseName='VideoProcessorQA-'+$Flavor+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$define=@();if($Flavor -eq 'config'){$define+='/DConfigOnly'}
if(-not $NoCompile){
 & $IsccPath @define "/DPayloadRoot=$PayloadRoot" "/DPayloadInclude=$includePath" "/DOutputRoot=$OutputDirectory" "/DCoreVersion=$($manifest.coreVersion)" "/DBuildCommit=fixture" "/DInstallerBaseName=$baseName" "/DFileVersion=$fileVersion" "/DSetupIcon=$root\images\VideoProcessor.ico" $issPath *> (Join-Path $OutputDirectory 'compile.log')
 if($LASTEXITCODE -ne 0){throw "Fixture Inno compilation failed: $OutputDirectory\compile.log"}
}
$installer=Join-Path $OutputDirectory ($baseName+'.exe')
if(-not $NoCompile -and -not (Test-Path -LiteralPath $installer)){throw 'Fixture Inno output is missing.'}
[pscustomobject]@{
 InstallerPath=$installer
 ManifestPath=$manifestPath
 PayloadIncludePath=$includePath
 ApplicationId=$(if($Flavor -eq 'full'){$fullId}else{$configId})
 ProductGuid=$(if($Flavor -eq 'full'){'0B37C49A-79D2-4D5D-9A7E-C99B31A2F001'}else{'7AD19C54-1F18-4227-8AF1-C99B31A2F002'})
 DisplayName=$label
 OutputDirectory=$OutputDirectory
}

