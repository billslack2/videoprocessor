param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$IntermediateDirectory,
    [Parameter(Mandatory=$true)][string]$CompilerPath,
    [Parameter(Mandatory=$true)][string]$IncludeDirectories,
    [Parameter(Mandatory=$true)][string]$LibraryDirectories,
    [Parameter(Mandatory=$true)][string]$ToolsetVersion
)
$ErrorActionPreference='Stop'
$vpRoot=[IO.Path]::GetFullPath($RepoRoot)
$vpOutput=[IO.Path]::GetFullPath($OutputDirectory)
$vpIntermediate=[IO.Path]::GetFullPath($IntermediateDirectory)
New-Item -ItemType Directory -Force -Path $vpOutput,$vpIntermediate | Out-Null
$vpCommit=(& git -C $vpRoot rev-parse HEAD).Trim()
if($LASTEXITCODE -ne 0 -or $vpCommit -notmatch '^[0-9a-f]{40}$'){throw 'Cannot identify stack probe source'}
$vpDirty=@(& git -C $vpRoot status --porcelain).Count -ne 0
$vpIdentity=Join-Path $vpIntermediate 'SubtitleStackProbeIdentity.h'
Set-Content -LiteralPath $vpIdentity -Encoding Ascii -Value ('#define VP_SUBTITLE_STACK_SOURCE_COMMIT "'+$vpCommit+'"')
$vpExecutable=Join-Path $vpOutput 'VideoProcessor-SubtitleStackProbe.exe'
$vpObject=Join-Path $vpIntermediate 'SubtitleNativeStackProbe.obj'
$vpPdb=Join-Path $vpOutput 'VideoProcessor-SubtitleStackProbe.pdb'
$env:INCLUDE=$IncludeDirectories
$env:LIB=$LibraryDirectories
$vpCompileArgs=@('/nologo','/DNOMINMAX','/DNDEBUG','/O2','/MD','/EHsc','/std:c++17','/Z7',
    ('/I'+(Join-Path $vpRoot 'src\VideoProcessor-Lib')),
    ('/I'+(Join-Path $vpRoot '3rdparty\libplacebo\include')),('/I'+$vpIntermediate),
    (Join-Path $vpRoot 'src\VideoProcessor-Test\SubtitleNativeStackProbe.cpp'),
    ('/Fo'+$vpObject),('/Fe'+$vpExecutable),'/link','/INCREMENTAL:NO','/LTCG','/DEBUG',
    ('/PDB:'+$vpPdb),'/STACK:1048576,4096','/SUBSYSTEM:CONSOLE',
    ('/LIBPATH:'+(Join-Path $vpRoot '3rdparty\libplacebo\lib')),'libplacebo-360.lib',
    (Join-Path (Split-Path -Parent $vpOutput) 'VideoProcessor-Lib.lib'))
& $CompilerPath @vpCompileArgs
if($LASTEXITCODE -ne 0){throw "Native stack probe compile failed: $LASTEXITCODE"}
$vpBytes=[IO.File]::ReadAllBytes($vpExecutable);$vpPe=[BitConverter]::ToInt32($vpBytes,0x3c)
$vpMachine=[BitConverter]::ToUInt16($vpBytes,$vpPe+4)
$vpOptional=$vpPe+24
$vpReserve=[BitConverter]::ToUInt64($vpBytes,$vpOptional+72)
$vpCommitBytes=[BitConverter]::ToUInt64($vpBytes,$vpOptional+80)
if($vpMachine -ne 0x8664 -or $vpReserve -ne 1048576 -or $vpCommitBytes -ne 4096){throw 'Native stack probe PE contract mismatch'}
$vpSha=[Security.Cryptography.SHA256]::Create()
try {$vpHash=[BitConverter]::ToString($vpSha.ComputeHash($vpBytes)).Replace('-','')} finally {$vpSha.Dispose()}
[pscustomobject]@{schemaVersion=1;sourceCommit=$vpCommit;sourceDirty=$vpDirty;configuration='Release';architecture='x64';
    toolsetVersion=$ToolsetVersion;binary=$vpExecutable;sha256=$vpHash;
    stackReserveBytes=$vpReserve;stackCommitBytes=$vpCommitBytes;callerPressureBytes=783808;
    testOnly=$true;builtAt=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content -LiteralPath ($vpExecutable+'.test.json')
