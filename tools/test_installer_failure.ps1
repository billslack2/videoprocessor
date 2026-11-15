[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$PayloadRoot,
 [Parameter(Mandatory=$true)][string]$IsccPath
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'installer_test_helpers.ps1')
$payload=(Resolve-Path -LiteralPath $PayloadRoot).Path
$IsccPath=(Resolve-Path -LiteralPath $IsccPath).Path
$qa=Join-Path $root ('artifacts\if-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$null=New-Item -ItemType Directory -Path $qa -Force
$fixture=& (Join-Path $PSScriptRoot 'prepare_installer_fixture.ps1') -PayloadRoot $payload -OutputDirectory (Join-Path $qa 'fixture') -IsccPath $IsccPath -Flavor full -NoCompile
$registry='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{'+$fixture.ProductGuid+'}_is1'
if(Test-VpTestRegistration $fixture.ProductGuid){throw 'An existing isolated fixture registration is present; refusing to touch it.'}
$bad=Get-Content -LiteralPath $fixture.ManifestPath -Raw | ConvertFrom-Json
($bad.files | Where-Object path -eq 'VideoProcessor.exe').sha256='0'*64
$bad | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $fixture.ManifestPath -Encoding UTF8
$numbers=($bad.coreVersion -split '-',2)[0].Split('.')
$fileVersion=if($numbers.Count -eq 3){($numbers -join '.')+'.0'}else{'1.0.0.0'}
$base='DO-NOT-DISTRIBUTE-corrupt-manifest-test'
$iss=Join-Path $fixture.OutputDirectory 'fixture.iss'
& $IsccPath "/DPayloadRoot=$payload" "/DPayloadInclude=$($fixture.PayloadIncludePath)" "/DOutputRoot=$($fixture.OutputDirectory)" "/DCoreVersion=$($bad.coreVersion)" '/DBuildCommit=fixture' "/DInstallerBaseName=$base" "/DFileVersion=$fileVersion" "/DSetupIcon=$root\images\VideoProcessor.ico" $iss *> (Join-Path $qa 'compile.log')
if($LASTEXITCODE -ne 0){throw "Failure-test compilation failed: $qa\compile.log"}
$installer=Join-Path $fixture.OutputDirectory ($base+'.exe')
$target=Join-Path $env:TEMP ('vpif-'+[guid]::NewGuid().ToString('N').Substring(0,8))
$null=New-Item -ItemType Directory -Path $target -Force
[IO.File]::WriteAllText((Join-Path $target 'VideoProcessor.cfg'),'# preserved data before failed setup')
[IO.File]::WriteAllText((Join-Path $target 'VideoProcessor.state'),'operator state')
[IO.File]::WriteAllText((Join-Path $target 'VideoProcessor.exe'),'previous application bytes')
$before=@{}
foreach($name in @('VideoProcessor.cfg','VideoProcessor.state','VideoProcessor.exe')){
 $before[$name]=(Get-FileHash -LiteralPath (Join-Path $target $name)).Hash
}
$process=Start-Process -FilePath $installer -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/DIR="'+$target+'"'),('/LOG="'+(Join-Path $qa 'failure.log')+'"')) -WindowStyle Hidden -Wait -PassThru
if($process.ExitCode -eq 0){throw 'Corrupt installer falsely reported success'}
if(Test-VpTestRegistration $fixture.ProductGuid){throw 'Corrupt installer created an uninstall entry'}
foreach($name in $before.Keys){
 if((Get-FileHash -LiteralPath (Join-Path $target $name)).Hash -ne $before[$name]){
  throw "Failure recovery did not preserve $name"
 }
}
$safeTarget=[IO.Path]::GetFullPath($target)
$tempRoot=[IO.Path]::GetFullPath($env:TEMP).TrimEnd('\')+'\'
if(-not $safeTarget.StartsWith($tempRoot,[StringComparison]::OrdinalIgnoreCase) -or
   [IO.Path]::GetFileName($safeTarget) -notmatch '^vpif-[0-9a-f]{8}$'){throw 'Unsafe failure fixture cleanup path'}
Remove-Item -LiteralPath $safeTarget -Recurse -Force
Write-Host "PASS actual fixture installer hash failure: nonzero exit, no registration, prior application restored, cfg/state unchanged. Evidence: $qa"
