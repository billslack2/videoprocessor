[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [string]$BuildRoot,
    [string]$StageRoot,
    [string]$VcRedistPath = $env:VP_VC_REDIST_X64,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifactRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'artifacts'))
if (-not $BuildRoot) {
    $BuildRoot = Join-Path $repositoryRoot 'x64\Release'
}
if (-not $StageRoot) {
    $StageRoot = Join-Path $artifactRoot 'release\VideoProcessor'
}
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$StageRoot = [IO.Path]::GetFullPath($StageRoot)
$artifactPrefix = $artifactRoot.TrimEnd('\') + '\'
if (-not $StageRoot.StartsWith($artifactPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "StageRoot must remain under the repository artifact directory: $artifactRoot"
}
if ($StageRoot -eq $artifactRoot -or $StageRoot -eq $repositoryRoot -or
    $StageRoot -eq $BuildRoot -or [IO.Path]::GetPathRoot($StageRoot) -eq $StageRoot) {
    throw "Refusing unsafe staging path: $StageRoot"
}

if (-not $VcRedistPath -or -not (Test-Path -LiteralPath $VcRedistPath -PathType Leaf)) {
    throw 'Pass -VcRedistPath with the official vc_redist.x64.exe (or set VP_VC_REDIST_X64). See docs/VP-0107_RELEASE_LAYOUT.md.'
}
$VcRedistPath = [IO.Path]::GetFullPath($VcRedistPath)
. (Join-Path $repositoryRoot 'packaging\prerequisites\runtime-common.ps1')
. (Join-Path $PSScriptRoot 'runtime_packaging.ps1')

$manifestPath = Join-Path $repositoryRoot 'packaging\release-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or -not $manifest.files) {
    throw 'Unsupported or empty release manifest.'
}

$sourceRoots = @{
    build = $BuildRoot
    repository = $repositoryRoot
    libplacebo = Join-Path $repositoryRoot '3rdparty\libplacebo'
    nvapi = Join-Path $repositoryRoot '3rdparty\nvapi'
    vcRedist = Split-Path -Parent $VcRedistPath
}
$expected = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$expectedDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in @($manifest.directories)) {
    $relativeDirectory = ([string]$entry.path).Replace('/', '\').TrimEnd('\')
    if (-not $relativeDirectory -or [IO.Path]::IsPathRooted($relativeDirectory) -or
        $relativeDirectory.Split('\') -contains '..' -or
        -not $expectedDirectories.Add($relativeDirectory)) {
        throw "Unsafe or duplicate manifest directory: $relativeDirectory"
    }
}
$copyPlan = foreach ($entry in $manifest.files) {
    if (-not $sourceRoots.ContainsKey([string]$entry.sourceRoot)) {
        throw "Unknown source root '$($entry.sourceRoot)' for $($entry.destination)"
    }
    $source = if ($entry.sourceRoot -eq 'vcRedist') { $VcRedistPath } else {
        [IO.Path]::GetFullPath((Join-Path $sourceRoots[[string]$entry.sourceRoot] ([string]$entry.source)))
    }
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required release source is missing: $source"
    }
    $relativeDestination = ([string]$entry.destination).Replace('/', '\')
    if ([IO.Path]::IsPathRooted($relativeDestination) -or $relativeDestination -match '(^|\\)\.\.(\\|$)') {
        throw "Unsafe manifest destination: $relativeDestination"
    }
    if (-not $expected.Add($relativeDestination)) {
        throw "Duplicate manifest destination: $relativeDestination"
    }
    $destination = [IO.Path]::GetFullPath((Join-Path $StageRoot $relativeDestination))
    if (-not $destination.StartsWith($StageRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Manifest destination escapes staging root: $relativeDestination"
    }
    [pscustomobject]@{
        Source = $source
        Destination = $destination
        RelativeDestination = $relativeDestination
    }
}

$runtimeRequirement = Get-VpPackageRuntimeRequirement -CopyPlan $copyPlan `
    -Policy $manifest.vcRuntime -InstallerPath $VcRedistPath
$runtimeMetadataPath = 'prerequisites\runtime-requirement.json'
if (@($manifest.generatedFiles).Count -ne 1 -or
    $manifest.generatedFiles[0] -ne $runtimeMetadataPath.Replace('\', '/') -or
    -not $expected.Add($runtimeMetadataPath)) {
    throw 'The manifest must declare exactly the generated runtime requirement file.'
}
foreach ($required in @('START-HERE.txt', 'SETUP-RUNTIME.cmd',
    'prerequisites\setup-runtime.ps1', 'prerequisites\runtime-common.ps1',
    'prerequisites\vc_redist.x64.exe')) {
    if (-not $expected.Contains($required)) { throw "Missing required setup file: $required" }
}
if (@($expected | Where-Object {
    [IO.Path]::GetFileName($_) -match '^(msvcp140.*|vcruntime140.*|mfc140.*|concrt140)\.dll$'
}).Count) { throw 'Ship the official runtime installer, not loose Microsoft runtime DLLs.' }

$privateDlls = @(
    'libdovi.dll', 'libgcc_s_seh-1.dll', 'liblcms2-2.dll',
    'libplacebo-360.dll', 'libshaderc_shared.dll',
    'libspirv-cross-c-shared.dll', 'libstdc++-6.dll',
    'libwinpthread-1.dll', 'vulkan-1.dll'
)
foreach ($name in $privateDlls) {
    if ($expected.Contains($name)) {
        throw "Plugin-private dependency is incorrectly staged at release root: $name"
    }
    if (-not $expected.Contains("vprenderer\$name")) {
        throw "Plugin-private dependency is missing from the canonical plugin directory: $name"
    }
}
if ($expected.Contains('config\platforms\qoffscreen.dll')) {
    throw 'The Config test-only qoffscreen plugin must not ship.'
}
$configRuntime = @(
    'config\VideoProcessorConfig.exe',
    'config\VideoProcessorConfigDiscovery.dll',
    'config\Qt6Core.dll', 'config\Qt6Gui.dll',
    'config\Qt6Widgets.dll', 'config\platforms\qwindows.dll'
)
foreach ($path in $configRuntime) {
    if (-not $expected.Contains($path)) {
        throw "Required private Config runtime file is missing: $path"
    }
}
if (@($expected | Where-Object {
    $_ -match '^(VideoProcessorConfig(?:Discovery)?\.(?:exe|dll)|Qt6.*\.dll|opengl32sw\.dll)$' -or
    $_ -match '^(generic|iconengines|imageformats|networkinformation|platforms|styles|tls)\\'
}).Count -ne 0) {
    throw 'Config/Qt runtime files and plugin folders must not be staged at release root.'
}
if ($expected.Contains('VideoProcessor.cfg') -or
    -not $expected.Contains('VideoProcessor.cfg.example')) {
    throw 'Release staging must contain only the example configuration name.'
}
if ($expected.Contains('CONFIGURATION.html') -or
    -not $expected.Contains('docs\CONFIGURATION.html') -or
    -not $expected.Contains('docs\VideoProcessor_NLS_Configuration_Guide.pdf')) {
    throw 'Release operator documentation must contain the configuration HTML and NLS PDF under docs only.'
}
$shaderDestinations = @($copyPlan | Where-Object {
    $_.RelativeDestination -like 'shaders\*'
})
if ($shaderDestinations.Count -ne 7 -or
    @($copyPlan | Where-Object {
        $_.RelativeDestination -like 'vprenderer\shaders\*'
    }).Count -ne 0) {
    throw 'The release must contain exactly one seven-file shader tree at the application root.'
}

Write-Host "Microsoft x64 runtime minimum: $($runtimeRequirement.minimumVersion); installer: $($runtimeRequirement.installerVersion)"
Write-Host "VP release manifest: $($expected.Count) immutable/generated files"
Write-Host "Build root: $BuildRoot"
Write-Host "Stage root: $StageRoot"
if ($DryRun) {
    $expectedDirectories | ForEach-Object {
        Write-Host "DRY RUN DIRECTORY $_"
    }
    $copyPlan | ForEach-Object {
        Write-Host "DRY RUN $($_.RelativeDestination) <- $($_.Source)"
    }
    Write-Host "DRY RUN GENERATED $runtimeMetadataPath"
    Write-Host 'DRY RUN complete: no files were written and no deployment/configuration path was accessed.'
    return
}

if ($BuildRoot.StartsWith($StageRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'StageRoot must not contain the build inputs.'
}
if (-not $PSCmdlet.ShouldProcess($StageRoot, 'Replace generated release staging tree')) { return }
if (Test-Path -LiteralPath $StageRoot) {
    Remove-Item -LiteralPath $StageRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $StageRoot | Out-Null
foreach ($relativeDirectory in $expectedDirectories) {
    New-Item -ItemType Directory -Path (Join-Path $StageRoot $relativeDirectory) -Force | Out-Null
}
foreach ($item in $copyPlan) {
    $parent = Split-Path -Parent $item.Destination
    if (-not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    Copy-Item -LiteralPath $item.Source -Destination $item.Destination
}

$runtimeRequirement | ConvertTo-Json -Depth 8 | Set-Content `
    -LiteralPath (Join-Path $StageRoot $runtimeMetadataPath) -Encoding UTF8

$actual = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
Get-ChildItem -LiteralPath $StageRoot -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($StageRoot.TrimEnd('\').Length + 1)
    [void]$actual.Add($relative)
    $licenseText = $relative.StartsWith(
        'vprenderer\third_party_licenses\',
        [StringComparison]::OrdinalIgnoreCase)
    if ((-not $licenseText -and
            $_.Extension -in @('.pdb', '.lib', '.exp', '.ilk', '.bak')) -or
        $_.Name -like '*.before-*' -or $_.Name -like '*.pre-*' -or
        $_.Name -eq 'VideoProcessorShaderCache.bin') {
        throw "Forbidden stale/build/mutable artifact in release: $relative"
    }
}
$missing = @($expected | Where-Object { -not $actual.Contains($_) })
$unexpected = @($actual | Where-Object { -not $expected.Contains($_) })
if ($missing.Count -gt 0 -or $unexpected.Count -gt 0) {
    throw "Release verification failed. Missing=[$($missing -join ', ')] Unexpected=[$($unexpected -join ', ')]"
}
foreach ($relativeDirectory in $expectedDirectories) {
    $directory = Join-Path $StageRoot $relativeDirectory
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        throw "Required release directory is missing: $relativeDirectory"
    }
}

Write-Host "Release staged and verified: $($actual.Count) files"
Write-Host 'Active configuration was not read or modified; only VideoProcessor.cfg.example was staged.'
