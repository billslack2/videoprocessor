[CmdletBinding()]
param(
 [Parameter(Mandatory)][string]$Checkout,
 [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{40}$')][string]$ExpectedCommit,
 [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{40}$')][string]$BetaCommit,
 [ValidatePattern('^[a-f0-9]{40}$')][string]$InstallerToolingCommit,
 [Parameter(Mandatory)][string]$CoreVersion,
 [Parameter(Mandatory)][string]$VsInstallPath,
 [Parameter(Mandatory)][string]$QtRoot,
 [Parameter(Mandatory)][string]$VcRedistPath,
 [Parameter(Mandatory)][string]$IsccPath,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [switch]$PortableZip,
 [switch]$ReuseVerifiedBuild
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Git-Checked([string[]]$Arguments) {
 $result = & git -C $Checkout @Arguments
 if ($LASTEXITCODE -ne 0) { throw "git failed: $($Arguments -join ' ')" }
 return $result
}
$Checkout = (Resolve-Path -LiteralPath $Checkout).Path
$origin = (Git-Checked @('remote','get-url','origin')) -join ''
if ($origin -notmatch '^(https://github.com/|git@github.com:)billslack2/videoprocessor(?:\.git)?$') { throw 'Unexpected origin.' }
if (((Git-Checked @('rev-parse','HEAD')) -join '') -ne $ExpectedCommit) { throw 'Source commit mismatch.' }
if (Git-Checked @('status','--porcelain')) { throw 'Release requires clean sources.' }
Git-Checked @('merge-base','--is-ancestor',$BetaCommit,$ExpectedCommit) | Out-Null
if ($InstallerToolingCommit) {
 Git-Checked @('merge-base','--is-ancestor',$InstallerToolingCommit,$ExpectedCommit) | Out-Null
 # A separately pinned packaging branch must not silently change application sources.
 if (Git-Checked @('diff','--name-only',$BetaCommit,$ExpectedCommit,'--','src')) { throw 'Application sources differ from the selected beta.' }
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output directory; previous artifacts will not be overwritten.' }
$outputFull = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
if ($outputFull.Equals($Checkout,[StringComparison]::OrdinalIgnoreCase) -or $outputFull.StartsWith($Checkout + '\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Distribution output must be outside the source checkout.' }
$msbuild = Join-Path $VsInstallPath 'MSBuild\Current\Bin\MSBuild.exe'
$vstest = Join-Path $VsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\TestWindow\vstest.console.exe'
$devshell = Join-Path $VsInstallPath 'Common7\Tools\Launch-VsDevShell.ps1'
foreach ($path in @($msbuild,$vstest,$devshell,$VcRedistPath,$IsccPath,(Join-Path $QtRoot 'bin\qmake.exe'))) {
 if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing tool: $path" }
}
# The compiler engine version is verified from its compilation log below.
$previousQt = $env:VP_QT_ROOT
$env:VP_QT_ROOT = $QtRoot
& $devshell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Host
$evidence = Join-Path $Checkout ('artifacts\release-verification-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$invocation = [ordered]@{ checkout=$Checkout; sourceCommit=$ExpectedCommit; betaCommit=$BetaCommit; installerToolingCommit=$InstallerToolingCommit; coreVersion=$CoreVersion; vsInstallPath=$VsInstallPath; qtRoot=$QtRoot; vcRedistPath=$VcRedistPath; isccPath=$IsccPath; portableZip=[bool]$PortableZip; reuseVerifiedBuild=[bool]$ReuseVerifiedBuild; workflowSha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash; outputDirectory=$outputFull }
$invocation | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'inputs.json') -Encoding UTF8
try {
 & (Join-Path $Checkout 'tools\build_installer.ps1') -CoreVersion $CoreVersion -VcRedistPath $VcRedistPath -IsccPath $IsccPath -MSBuildPath $msbuild -PortableZip:$PortableZip -SkipBuild:$ReuseVerifiedBuild *> (Join-Path $evidence 'build-and-package.log')
 if (-not (Select-String -LiteralPath (Join-Path $evidence 'build-and-package.log') -Pattern 'Compiler engine version: Inno Setup 6\.7\.3$' -Quiet)) { throw 'Unexpected Inno compiler version.' }
 $testArgs = @((Join-Path $Checkout 'x64\Release\VideoProcessor-Test.dll'),'/Platform:x64',"/ResultsDirectory:$evidence",'/Logger:trx;LogFileName=release.trx')
 # VSTest writes diagnostics to stderr on failing tests; preserve output, then check exit status.
 $ErrorActionPreference = 'Continue'
 & $vstest @testArgs *> (Join-Path $evidence 'tests.log')
 $testExit = $LASTEXITCODE
 $ErrorActionPreference = 'Stop'
 if ($testExit -ne 0) { throw "Full test suite failed ($testExit). See $evidence. No distribution exported." }
 [xml]$trx = Get-Content -LiteralPath (Join-Path $evidence 'release.trx') -Raw
 $counts = $trx.TestRun.ResultSummary.Counters
 if ([int]$counts.total -le 0 -or [int]$counts.passed -ne [int]$counts.total) { throw 'Test run incomplete or unsuccessful.' }
 foreach ($name in @('test_installer_support','test_installer_identity')) {
  & (Join-Path $Checkout "tools\$name.ps1") *> (Join-Path $evidence "$name.log")
 }
 & (Join-Path $Checkout 'tools\test_runtime_packaging.ps1') -VcRedistPath $VcRedistPath *> (Join-Path $evidence 'runtime-packaging.log')
 & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Checkout 'artifacts\release\VideoProcessor\prerequisites\setup-runtime.ps1') -CheckOnly *> (Join-Path $evidence 'runtime-check.log')
 $legacyRuntimeCheckExit = $LASTEXITCODE # Diagnostic only: distribution uses verified app-local runtimes.
 . (Join-Path $Checkout 'tools\installer_build_identity.ps1')
 $identity = Get-VpSourceIdentity $Checkout
 if ($identity.dirty -or $identity.commit -ne $ExpectedCommit) { throw 'Sources changed during qualification.' }
 $manifestFile = Join-Path $Checkout 'artifacts\release\VideoProcessor\INSTALL-MANIFEST.json'
 $manifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
 if ($manifest.sourceCommit -ne $ExpectedCommit -or $manifest.sourceFingerprint -ne $identity.fingerprint -or $manifest.dirty) { throw 'Payload provenance mismatch.' }
 foreach ($entry in $manifest.files) {
  $payloadFile = Join-Path $Checkout ('artifacts\release\VideoProcessor\' + $entry.path)
  if ((Get-FileHash -LiteralPath $payloadFile -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Payload hash mismatch: $($entry.path)" }
 }
 $names = @((Get-VpInstallerBaseName $CoreVersion $identity) + '.exe')
 if ($PortableZip) { $names += "VideoProcessor-$CoreVersion-$($ExpectedCommit.Substring(0,12))-x64-Portable.zip" }
 $artifacts = @(foreach ($name in $names) {
  $file = Join-Path $Checkout "artifacts\installers\$name"
  $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
  $sidecar = (Get-Content -LiteralPath ($file + '.sha256') -Raw).Trim()
  if ($sidecar -ne "$hash  $name") { throw "Checksum sidecar mismatch: $name" }
  [ordered]@{ name=$name; sha256=$hash; length=(Get-Item -LiteralPath $file).Length }
 })
 $tools = @(foreach ($path in @($msbuild,$vstest,$IsccPath,$VcRedistPath,(Join-Path $QtRoot 'bin\Qt6Core.dll'))) {
  [ordered]@{ path=$path; version=(Get-Item -LiteralPath $path).VersionInfo.FileVersion; sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
 })
 $receipt = [ordered]@{ schema=1; inputs=$invocation; sourceFingerprint=$identity.fingerprint; testsPassed=[int]$counts.passed; legacyRuntimeCheckExit=$legacyRuntimeCheckExit; tools=$tools; artifacts=$artifacts; signed=$false; qualification='Full unit suite and packaging helper tests passed. Real installer lifecycle and clean Windows/interactive qualification must be reported separately.'; reproducibility='Pinned source and recorded tools/dependencies; byte-identical PE/installer rebuilds are not asserted.' }
 $receipt | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $evidence 'release-receipt.json') -Encoding UTF8
 New-Item -ItemType Directory -Path $outputFull | Out-Null
 foreach ($name in $names) {
  foreach ($suffix in @('','.sha256')) { Copy-Item -LiteralPath (Join-Path $Checkout "artifacts\installers\$name$suffix") -Destination $outputFull }
 }
 Copy-Item -LiteralPath $manifestFile -Destination $outputFull
 Copy-Item -LiteralPath (Join-Path $Checkout 'artifacts\installer-build.json') -Destination $outputFull
 Copy-Item -LiteralPath $evidence -Destination (Join-Path $outputFull 'validation') -Recurse
 foreach ($artifact in $artifacts) {
  if ((Get-FileHash -LiteralPath (Join-Path $outputFull $artifact.name)).Hash -ne $artifact.sha256) { throw 'Export hash mismatch.' }
 }
 Write-Host "Verified release exported: $outputFull ($($counts.passed) tests passed)"
} finally { $env:VP_QT_ROOT = $previousQt }
