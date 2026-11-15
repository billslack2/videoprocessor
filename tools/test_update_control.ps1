[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$scratch=Join-Path $env:TEMP ('vp-update-control-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $scratch
$rootList=Join-Path $scratch 'test-roots.txt'
$previousRootList=$env:VP_UPDATE_TEST_ROOT_LIST
try {
 $updater=Join-Path $scratch 'VideoProcessorUpdate.exe'
 $test=Join-Path $scratch 'UpdateControlTests.exe'
 & (Join-Path $PSScriptRoot 'build_updater.ps1') -OutputPath $updater
 if($LASTEXITCODE -ne 0){throw 'Updater build failed'}
 $compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
 & $compiler /nologo /target:exe /platform:x64 /optimize+ "/out:$test" "/r:$updater" /r:System.Web.Extensions.dll (Join-Path $root 'src\VideoProcessor-Update\UpdateControlTests.cs')
 if($LASTEXITCODE -ne 0){throw 'Update control test compilation failed'}
 $env:VP_UPDATE_TEST_ROOT_LIST=$rootList
 & $test $updater
 if($LASTEXITCODE -ne 0){throw 'Update control tests failed'}
}
finally {
 if($null -eq $previousRootList){Remove-Item Env:VP_UPDATE_TEST_ROOT_LIST -ErrorAction SilentlyContinue}else{$env:VP_UPDATE_TEST_ROOT_LIST=$previousRootList}
 if(Test-Path -LiteralPath $rootList) {
  $localData=[Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
  $cacheBase=[IO.Path]::GetFullPath((Join-Path $localData 'VideoProcessor\Updates')).TrimEnd('\')
  $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
  foreach($testRoot in [IO.File]::ReadAllLines($rootList)) {
   try {
    $fullRoot=[IO.Path]::GetFullPath($testRoot).TrimEnd('\')
    if(-not $fullRoot.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase) -or
       (Split-Path -Leaf $fullRoot) -notmatch '^vp-update-(start-race|control)-[0-9a-f]{32}$') { throw 'Test cache root did not match the generated temporary-root contract.' }
    $sha=[Security.Cryptography.SHA256]::Create()
    try { $id=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($sid+'|'+$fullRoot.ToLowerInvariant())))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
    $target=[IO.Path]::GetFullPath((Join-Path $cacheBase $id))
    if([IO.Path]::GetDirectoryName($target).TrimEnd('\') -ne $cacheBase -or (Split-Path -Leaf $target) -notmatch '^[0-9a-f]{64}$') { throw 'Test cache path escaped its exact per-root location.' }
    if(Test-Path -LiteralPath $target) {
     $entries=@(Get-Item -LiteralPath $target -Force) + @(Get-ChildItem -LiteralPath $target -Force -Recurse)
     if($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'Test cache contains a reparse point; preserving it.' }
     Remove-Item -LiteralPath $target -Recurse -Force
    }
   } catch { Write-Warning ("Could not safely clean test cache for '$testRoot': "+$_.Exception.Message) }
  }
 }
 if($scratch.StartsWith([IO.Path]::GetTempPath(),[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $scratch)) { Remove-Item -LiteralPath $scratch -Recurse -Force }
}
