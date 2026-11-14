[CmdletBinding()]
param([string]$MSBuildPath, [string]$QtRoot)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $MSBuildPath) {
    $candidates = @(
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe')
    )
    $MSBuildPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (-not $MSBuildPath -or -not (Test-Path -LiteralPath $MSBuildPath)) { throw 'MSBuild.exe was not found; pass -MSBuildPath.' }
if (-not $QtRoot) { $QtRoot = $env:VP_QT_ROOT }
if (-not $QtRoot) { $QtRoot = Join-Path $env:LOCALAPPDATA 'VideoProcessor\dependencies\Qt\6.8.3\msvc2022_64' }
if (-not (Test-Path -LiteralPath (Join-Path $QtRoot 'bin\qmake.exe'))) { throw 'Qt 6.8.3 x64 was not found; pass -QtRoot or set VP_QT_ROOT.' }

$scratch = Join-Path $env:TEMP ('vp-update-panel-bridge-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $scratch
$rootList = Join-Path $scratch 'test-roots.txt'
$testOutput = Join-Path $scratch 'bin'
$testIntermediate = Join-Path $scratch 'obj'
$null = New-Item -ItemType Directory -Path $testOutput
$null = New-Item -ItemType Directory -Path $testIntermediate
$testExe = Join-Path $testOutput 'VideoProcessorConfigTests.exe'
$helper = Join-Path $scratch 'VideoProcessorUpdate.exe'
$previousHelper = $env:VP_UPDATE_HELPER_PATH
$previousPlatform = $env:QT_QPA_PLATFORM
$previousRootList = $env:VP_UPDATE_TEST_ROOT_LIST
try {
    & (Join-Path $PSScriptRoot 'build_updater.ps1') -OutputPath $helper
    if ($LASTEXITCODE -ne 0) { throw 'Updater build failed.' }
    $project = Join-Path $root 'src\VideoProcessor-ConfigTests\VideoProcessor-ConfigTests.vcxproj'
    & $MSBuildPath $project /t:Build /m:1 /p:Configuration=Release /p:Platform=x64 "/p:QtRoot=$QtRoot" "/p:OutDir=$testOutput\" "/p:IntDir=$testIntermediate\" /p:CL_MPCount=1 /p:AdditionalOptions=/FS /v:minimal
    if ($LASTEXITCODE -ne 0) { throw 'Config test build failed.' }
    $env:VP_UPDATE_HELPER_PATH = $helper
    $env:VP_UPDATE_TEST_ROOT_LIST = $rootList
    foreach ($testName in @(
        'integrated updates respect local package',
        'persistent update panel bridge',
        'update shutdown discards edits without prompt'
    )) {
        $env:QT_QPA_PLATFORM = if ($testName -eq 'update shutdown discards edits without prompt') { 'windows' } else { 'offscreen' }
        & $testExe --test $testName
        if ($LASTEXITCODE -ne 0) { throw "Config updater regression failed: $testName" }
    }
}
finally {
    if ($null -eq $previousHelper) { Remove-Item Env:VP_UPDATE_HELPER_PATH -ErrorAction SilentlyContinue }
    else { $env:VP_UPDATE_HELPER_PATH = $previousHelper }
    if ($null -eq $previousPlatform) { Remove-Item Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue }
    else { $env:QT_QPA_PLATFORM = $previousPlatform }
    if ($null -eq $previousRootList) { Remove-Item Env:VP_UPDATE_TEST_ROOT_LIST -ErrorAction SilentlyContinue }
    else { $env:VP_UPDATE_TEST_ROOT_LIST = $previousRootList }
    if (Test-Path -LiteralPath $rootList) {
        $localData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
        $cacheBase = [IO.Path]::GetFullPath((Join-Path $localData 'VideoProcessor\Updates')).TrimEnd('\')
        $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
        foreach ($testRoot in [IO.File]::ReadAllLines($rootList)) {
            try {
                $fullRoot = [IO.Path]::GetFullPath($testRoot).TrimEnd('\')
                $tempBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
                if (-not $fullRoot.StartsWith($tempBase + '\', [StringComparison]::OrdinalIgnoreCase) -or
                    (Split-Path -Leaf $fullRoot) -notmatch '^VideoProcessorConfigTests-[A-Za-z0-9_-]+$') { throw 'Test cache root did not match the generated temporary-root contract.' }
                $sha = [Security.Cryptography.SHA256]::Create()
                try { $id = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($sid + '|' + $fullRoot.ToLowerInvariant())))).Replace('-', '').ToLowerInvariant() }
                finally { $sha.Dispose() }
                $target = [IO.Path]::GetFullPath((Join-Path $cacheBase $id))
                if ([IO.Path]::GetDirectoryName($target).TrimEnd('\') -ne $cacheBase -or (Split-Path -Leaf $target) -notmatch '^[0-9a-f]{64}$') { throw 'Test cache path escaped its exact per-root location.' }
                if (Test-Path -LiteralPath $target) {
                    $entries = @(Get-Item -LiteralPath $target -Force) + @(Get-ChildItem -LiteralPath $target -Force -Recurse)
                    if ($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'Test cache contains a reparse point; preserving it.' }
                    Remove-Item -LiteralPath $target -Recurse -Force
                }
            } catch { Write-Warning ("Could not safely clean test cache for '$testRoot': " + $_.Exception.Message) }
        }
    }
    if ($scratch.StartsWith([IO.Path]::GetTempPath(), [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $scratch)) {
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}
