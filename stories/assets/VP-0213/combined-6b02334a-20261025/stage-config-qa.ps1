$ErrorActionPreference='Stop'
$vpRelease='E:\codex\videoprocessor\pr177-pr203-deploy-20261025\x64\Release'
$vpQa=Join-Path $vpRelease 'ConfigQA'
New-Item -ItemType Directory -Path $vpQa -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $vpRelease 'VideoProcessorConfigTests.exe') -Destination $vpQa -Force
foreach($vpDll in Get-ChildItem -LiteralPath $vpRelease -File -Filter '*.dll'){Copy-Item -LiteralPath $vpDll.FullName -Destination $vpQa -Force}
Copy-Item -LiteralPath (Join-Path $vpRelease 'platforms') -Destination $vpQa -Recurse -Force
if((Get-FileHash -LiteralPath (Join-Path $vpRelease 'VideoProcessorConfigTests.exe')).Hash -ne (Get-FileHash -LiteralPath (Join-Path $vpQa 'VideoProcessorConfigTests.exe')).Hash){throw 'Test executable staging mismatch'}
'ConfigQA staged from the combined Release build'