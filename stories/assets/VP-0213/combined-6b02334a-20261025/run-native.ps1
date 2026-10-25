param([ValidateSet('focus','related')][string]$Suite='focus')
$ErrorActionPreference='Stop'
$vpRoot='E:\codex\videoprocessor\pr177-pr203-deploy-20261025'
$vpResults='E:\codex\diagnostics\pr177-pr203-deploy-20261025'
$vpFilter='FullyQualifiedName~OffBarContentFitTests'
if($Suite -eq 'related') {
 $vpSources=@('AlphaSourceCropPolicyTests.cpp','ActivePictureMeasurementCacheTests.cpp','TemporalPresentationLedgerTests.cpp','ConfigFileTests.cpp','QualityProfileMigrationTests.cpp','LibplaceboRenderParametersTests.cpp')
 $vpClasses=@(foreach($vpSource in $vpSources){$vpText=Get-Content (Join-Path $vpRoot ('src/VideoProcessor-Test/'+$vpSource)) -Raw; [regex]::Matches($vpText,'TEST_CLASS\((\w+)\)') | ForEach-Object {$_.Groups[1].Value}})
 $vpClasses+=@('RendererResetPolicyTests','RendererProfileConfigTests','SubtitleAspectRetentionTests','StartupPresentationReadinessTests')
 $vpFilter=($vpClasses | Sort-Object -Unique | ForEach-Object {'FullyQualifiedName~'+$_}) -join '|'
}
$vpDll=Join-Path $vpRoot 'x64/Release/VideoProcessor-Test.dll'
if(-not (Test-Path -LiteralPath $vpDll)){throw 'Native Release test DLL is not ready'}
$vpArgs=@($vpDll,'/Platform:x64',('/TestCaseFilter:'+$vpFilter),('/ResultsDirectory:'+$vpResults),('/Logger:trx;LogFileName=off-'+$Suite+'.trx'),'/InIsolation')
& 'C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe' @vpArgs *> (Join-Path $vpResults ('off-'+$Suite+'-tests.log'))
$vpExit=$LASTEXITCODE
Get-Content (Join-Path $vpResults ('off-'+$Suite+'-tests.log')) -Tail 28
exit $vpExit

