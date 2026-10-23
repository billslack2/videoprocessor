param(
 [ValidateSet('Build','Run','All')][string]$Stage='All',
 [switch]$SharedReleaseBuildConfirmed,
 [switch]$NativeTestsFinished,
 [string]$SourceRoot,
 [string]$OutputDirectory=$PSScriptRoot,
 [string]$VisualStudioInstallation,
 [string]$MagewellSdkRoot,
 [string]$PythonExecutable
)
$ErrorActionPreference='Stop'
if(-not $SharedReleaseBuildConfirmed){throw 'Wait for root confirmation that the shared x64 Release build passed before linking or measuring.'}
if($Stage -ne 'Build' -and -not $NativeTestsFinished){throw 'Wait for root confirmation that native tests finished before running CPU measurements.'}
if(-not $SourceRoot){
 $vpProofSearch=Get-Item -LiteralPath $PSScriptRoot
 while($vpProofSearch -and -not (Test-Path -LiteralPath (Join-Path $vpProofSearch.FullName 'VideoProcessor.sln'))){$vpProofSearch=$vpProofSearch.Parent}
 if(-not $vpProofSearch){throw 'Pass -SourceRoot for the VP source checkout when the harness is stored outside it.'}
 $SourceRoot=$vpProofSearch.FullName
}
$vpProofRoot=(Resolve-Path -LiteralPath $SourceRoot).Path
if(-not (Test-Path -LiteralPath (Join-Path $vpProofRoot 'src\VideoProcessor-Test\VideoProcessor-Test.vcxproj.vcxproj'))){throw 'SourceRoot does not contain the VP native test project.'}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$vpProofOutput=(Resolve-Path -LiteralPath $OutputDirectory).Path
Set-Location -LiteralPath $vpProofRoot
if(-not $VisualStudioInstallation){
 if($env:VSINSTALLDIR){$VisualStudioInstallation=$env:VSINSTALLDIR}
 else{
  $vpProofVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
  if(-not (Test-Path -LiteralPath $vpProofVswhere)){throw 'Pass -VisualStudioInstallation or run from a Visual Studio developer shell.'}
  $VisualStudioInstallation=(& $vpProofVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath | Select-Object -First 1)
 }
}
if(-not $VisualStudioInstallation){throw 'No Visual Studio installation with MSBuild was found.'}
$vpProofDevShell=Join-Path $VisualStudioInstallation 'Common7\Tools\Launch-VsDevShell.ps1'
$vpProofVstest=Join-Path $VisualStudioInstallation 'Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe'
& $vpProofDevShell -VsInstallationPath $VisualStudioInstallation -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if($Stage -ne 'Run'){
 $vpProofIdentity=[ordered]@{capturedAt=(Get-Date).ToString('o');head=(& git rev-parse HEAD);status=(& git status --porcelain);method='Isolated cache-component/certificate benchmark; no renderer replay; project-reference builds disabled';sourceFiles=@()}
 foreach($vpProofRelative in @('src\VideoProcessor-Lib\ActivePictureEvidence.cpp','src\VideoProcessor-Lib\vprenderer\PreparedPictureEvidence.h','src\VideoProcessor-Lib\vprenderer\PreparedOutwardWindow.h','src\VideoProcessor-Lib\vprenderer\BufferedPictureExpansion.cpp','src\VideoProcessor-Lib\AnalysisEvaluationBudget.h','x64\Release\VideoProcessor-Lib.lib')){
  $vpProofFull=Join-Path $vpProofRoot $vpProofRelative
  if(Test-Path -LiteralPath $vpProofFull){$vpProofIdentity.sourceFiles += [ordered]@{path=$vpProofRelative;sha256=(Get-FileHash -LiteralPath $vpProofFull -Algorithm SHA256).Hash;lastWrite=(Get-Item -LiteralPath $vpProofFull).LastWriteTime.ToString('o')}}
 }
 $vpProofArguments=@('src\VideoProcessor-Test\VideoProcessor-Test.vcxproj.vcxproj','/t:Build','/m:1','/p:BuildProjectReferences=false',("/p:SolutionDir=$vpProofRoot\"),'/p:Configuration=Release','/p:Platform=x64',("/p:ForceImportBeforeCppTargets=$PSScriptRoot\proof-delivery-hook.targets"),'/v:minimal','/nologo')
 if($MagewellSdkRoot){$vpProofArguments += "/p:MagewellSdkRoot=$MagewellSdkRoot"}
 & MSBuild.exe @vpProofArguments *> (Join-Path $vpProofOutput 'measurement-build.log')
 $vpProofBuildExit=$LASTEXITCODE
 if($vpProofBuildExit -ne 0){Get-Content (Join-Path $vpProofOutput 'measurement-build.log') -Tail 40;exit $vpProofBuildExit}
 $vpProofIdentity.measurementDll=[ordered]@{path=(Join-Path $PSScriptRoot 'bin\VP0205ProofDeliveryMeasurements.dll');sha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'bin\VP0205ProofDeliveryMeasurements.dll') -Algorithm SHA256).Hash}
 $vpProofIdentity | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $vpProofOutput 'measurement-source-identity.json') -Encoding UTF8
}
if($Stage -eq 'Build'){Write-Output 'Isolated harness linked. Measurements were not run.';exit 0}
$vpProofIdentityPath=Join-Path $vpProofOutput 'measurement-source-identity.json'
if(-not (Test-Path -LiteralPath $vpProofIdentityPath)){throw 'Build this harness first; no measurement-source-identity.json exists in OutputDirectory.'}
$vpProofIdentity=Get-Content -LiteralPath $vpProofIdentityPath -Raw | ConvertFrom-Json
foreach($vpProofFile in $vpProofIdentity.sourceFiles){
 if((Get-FileHash -LiteralPath (Join-Path $vpProofRoot $vpProofFile.path) -Algorithm SHA256).Hash -ne $vpProofFile.sha256){throw ('Source/build input changed after harness link: '+$vpProofFile.path)}
}
$vpProofDll=Join-Path $PSScriptRoot 'bin\VP0205ProofDeliveryMeasurements.dll'
if((Get-FileHash -LiteralPath $vpProofDll -Algorithm SHA256).Hash -ne $vpProofIdentity.measurementDll.sha256){throw 'Measurement DLL changed after recorded link.'}
$vpProofPreviousOutput=$env:VP_LOOKAHEAD_MEASUREMENT_OUTPUT_DIR
try{
 $env:VP_LOOKAHEAD_MEASUREMENT_OUTPUT_DIR=$vpProofOutput
 & $vpProofVstest $vpProofDll '/Logger:trx;LogFileName=proof-delivery.trx' ("/ResultsDirectory:$vpProofOutput\results") *> (Join-Path $vpProofOutput 'measurement-run.log')
 $vpProofRunExit=$LASTEXITCODE
}finally{$env:VP_LOOKAHEAD_MEASUREMENT_OUTPUT_DIR=$vpProofPreviousOutput}
Get-Content (Join-Path $vpProofOutput 'measurement-run.log') -Tail 35
if($vpProofRunExit -eq 0){
 if(-not $PythonExecutable){$vpProofPython=Get-Command python.exe -ErrorAction SilentlyContinue;if($vpProofPython){$PythonExecutable=$vpProofPython.Source}else{throw 'Pass -PythonExecutable for the installed Python interpreter.'}}
 & $PythonExecutable (Join-Path $PSScriptRoot 'summarize-proof-delivery.py') $vpProofOutput
 if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
}
exit $vpProofRunExit
