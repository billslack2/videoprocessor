[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$PayloadRoot,
 [Parameter(Mandatory=$true)][string]$IsccPath
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
$payload=(Resolve-Path -LiteralPath $PayloadRoot).Path
$IsccPath=(Resolve-Path -LiteralPath $IsccPath).Path
$registry='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{42D852F1-70E9-43ED-8739-D61752106D59}_is1'
if(Test-Path $registry){throw 'Use a disposable account without an existing VP installer registration.'}
$qa=Join-Path $root ('artifacts\installer-failure-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $qa -Force
$bad=Get-Content -LiteralPath (Join-Path $payload 'INSTALL-MANIFEST.json') -Raw | ConvertFrom-Json
($bad.files | Where-Object path -eq 'VideoProcessor.exe').sha256='0'*64
$bad | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $qa 'INSTALL-MANIFEST.json') -Encoding UTF8
$iss=Get-Content (Join-Path $root 'packaging\installer\VideoProcessor.iss') -Raw
$iss=$iss.Replace('Source: "{#PayloadRoot}\INSTALL-MANIFEST.json"; Flags: dontcopy',
    'Source: "'+(Join-Path $qa 'INSTALL-MANIFEST.json')+'"; Flags: dontcopy')
$iss=$iss.Replace('OutputBaseFilename=VideoProcessor-{#CoreVersion}-{#BuildCommit}-x64-Setup',
    'OutputBaseFilename=DO-NOT-DISTRIBUTE-corrupt-manifest-test')
$iss | Set-Content -LiteralPath (Join-Path $qa 'corrupt.iss') -Encoding UTF8
& $IsccPath "/DPayloadRoot=$payload" "/DPayloadInclude=$root\artifacts\installer-payload.iss" "/DOutputRoot=$qa" "/DCoreVersion=$($bad.coreVersion)" "/DBuildCommit=$($bad.build.Substring(0,12))" (Join-Path $qa 'corrupt.iss') *> (Join-Path $qa 'compile.log')
if($LASTEXITCODE -ne 0){throw "Failure-test compilation failed: $qa\compile.log"}
$target=Join-Path $qa 'failure-target'
$null=New-Item -ItemType Directory -Path $target -Force
[IO.File]::WriteAllText((Join-Path $target 'VideoProcessor.cfg'),'# preserved data before failed setup')
[IO.File]::WriteAllText((Join-Path $target 'VideoProcessor.state'),'operator state')
[IO.File]::WriteAllText((Join-Path $target 'VideoProcessor.exe'),'previous application bytes')
$before=@{}
foreach($name in @('VideoProcessor.cfg','VideoProcessor.state','VideoProcessor.exe')){
 $before[$name]=(Get-FileHash -LiteralPath (Join-Path $target $name)).Hash
}
$p=Start-Process -FilePath (Join-Path $qa 'DO-NOT-DISTRIBUTE-corrupt-manifest-test.exe') -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-',('/DIR="'+$target+'"'),('/LOG="'+(Join-Path $qa 'failure.log')+'"')) -WindowStyle Hidden -Wait -PassThru
if($p.ExitCode -eq 0){throw 'Corrupt installer falsely reported success'}
if(Test-Path $registry){throw 'Corrupt installer created an uninstall entry'}
foreach($name in $before.Keys){
 if((Get-FileHash -LiteralPath (Join-Path $target $name)).Hash -ne $before[$name]){
  throw "Failure recovery did not preserve $name"
 }
}
Write-Host "PASS actual installer hash failure: nonzero exit, no registration, previous application restored, config/state unchanged. Evidence: $qa"
