[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$CaptureDirectory,
    [string]$Root=(Split-Path -Parent $PSScriptRoot),
    [string]$HeaderRoot,
    [string]$Library,
    [string]$OutputDirectory,
    [string]$MSBuildPath='C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe',
    [switch]$RequirePass
)
$ErrorActionPreference='Stop'
[Diagnostics.Process]::GetCurrentProcess().PriorityClass='BelowNormal'
$Root=[IO.Path]::GetFullPath($Root)
if(-not $HeaderRoot){$HeaderRoot=$Root};$HeaderRoot=[IO.Path]::GetFullPath($HeaderRoot)
if(-not $Library){$Library=Join-Path $Root 'x64\Release\VideoProcessor-Lib.lib'}
if(-not $OutputDirectory){$OutputDirectory=Join-Path $Root ('artifacts\subtitle-tolerance\'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(-not (Test-Path -LiteralPath $Library -PathType Leaf)){throw 'Build the native library before this harness; it never triggers a concurrent library rebuild.'}
if(-not (Test-Path -LiteralPath $CaptureDirectory -PathType Container)){throw 'Native capture directory is unavailable.'}
$null=New-Item -ItemType Directory -Force -Path $OutputDirectory
$sourcePin=(& git -C $Root rev-parse HEAD).Trim()
$sourceDirtyBefore=@(& git -C $Root status --porcelain)
$headerReceipt=Join-Path $HeaderRoot 'source.json'
$headerSnapshot=if(Test-Path -LiteralPath $headerReceipt){Get-Content -LiteralPath $headerReceipt -Raw|ConvertFrom-Json}else{$null}
$headerPin=if($headerSnapshot){$headerSnapshot.sourceCommit}else{$sourcePin}
$headers=@(Get-ChildItem -LiteralPath (Join-Path $HeaderRoot 'src\VideoProcessor-Lib') -Recurse -File|Where-Object Extension -In '.h','.cpp'|Sort-Object FullName|ForEach-Object {
    [ordered]@{path=$_.FullName.Substring($HeaderRoot.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
$harnessFiles=@('tools\subtitle_tolerance_replay.cpp','tools\subtitle_tolerance_replay.vcxproj','tools\test_subtitle_tolerance_capture.ps1')
$harness=@($harnessFiles|ForEach-Object {[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $Root $_)).Hash}})
$libraryHash=(Get-FileHash -LiteralPath $Library -Algorithm SHA256).Hash
$buildArgs=@((Join-Path $Root 'tools\subtitle_tolerance_replay.vcxproj'),'/p:Configuration=Release','/p:Platform=x64','/m:1','/nr:false','/v:minimal',
    "/p:ReplaySourceRoot=$HeaderRoot","/p:ReplayLibrary=$Library","/p:ReplayOutputRoot=$OutputDirectory",
    "/flp:logfile=$OutputDirectory\build.log;verbosity=minimal")
& $MSBuildPath @buildArgs
if($LASTEXITCODE -ne 0){throw "CPU replay build failed: $LASTEXITCODE"}
$exe=Join-Path $OutputDirectory 'SubtitleToleranceReplay.exe'
$output=Join-Path $OutputDirectory 'replay.jsonl'
$argsForReplay=@($CaptureDirectory);if($RequirePass){$argsForReplay+='--require-pass'}
& $exe @argsForReplay > $output
$replayExit=$LASTEXITCODE
$sourcePinAfter=(& git -C $Root rev-parse HEAD).Trim()
if($sourcePinAfter -ne $sourcePin){throw 'Source commit changed during replay; discard this run and repeat after the source freeze.'}
$headerChanges=@($headers|Where-Object {(Get-FileHash -LiteralPath (Join-Path $HeaderRoot $_.path)).Hash -ne $_.sha256})
$harnessChanges=@($harness|Where-Object {(Get-FileHash -LiteralPath (Join-Path $Root $_.path)).Hash -ne $_.sha256})
if($headerChanges.Count -or $harnessChanges.Count -or (Get-FileHash -LiteralPath $Library).Hash -ne $libraryHash){throw 'Replay source or library changed during the experiment; discard this run.'}
$captures=@(Get-ChildItem -LiteralPath $CaptureDirectory -File|Where-Object Name -Match '^frame-\d+\.(bin|txt)$'|Sort-Object Name|ForEach-Object {
    [ordered]@{name=$_.Name;bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
})
$summary=@(Get-Content -LiteralPath $output|ForEach-Object {$_|ConvertFrom-Json}|Where-Object {$_.kind -In 'capture','cost','matrix_summary','timeline_summary','worker_summary','result'})
[ordered]@{
    schema=1;sourceCommit=$sourcePin;headersCommit=$headerPin;headerRoot=$HeaderRoot;
    sourceDirty=$sourceDirtyBefore;sourceCommitAfter=$sourcePinAfter;headerSnapshot=$headerSnapshot;harness=$harness;headers=$headers;
    library=[ordered]@{path=$Library;sha256=$libraryHash};binarySha256=(Get-FileHash -LiteralPath $exe).Hash;
    captureDirectory=$CaptureDirectory;captures=$captures;summary=$summary;replayExit=$replayExit;
    requirePass=[bool]$RequirePass;configuration='Release';architecture='x64';cpuOnly=$true;gpuQualified=$false;
    limitations=@('Seed acquisition uses synthetic three-source copies of each captured phase.',
        'Footprint matrix preserves later captured identities and original immutable seed pixels.',
        'Tracker and worker timelines use explicitly synthetic consecutive identities over captured pixels.',
        'Worker requests offer real full scans; source preparation/composition success is modeled only.',
        'Prepared admission models successful composition; it does not render GPU pixels or establish live presentation cadence.',
        'Operation wall times exclude source IO and must not be interpreted as end-to-end frame deadlines.');
    finishedUtc=[DateTime]::UtcNow.ToString('o')
}|ConvertTo-Json -Depth 9|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding UTF8
$summary|ConvertTo-Json -Depth 5
exit $replayExit
