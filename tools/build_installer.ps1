[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$')][string]$CoreVersion,
    [Parameter(Mandatory=$true)][string]$VcRedistPath,
    [Parameter(Mandatory=$true)][string]$IsccPath,
    [string]$MSBuildPath, [switch]$SkipBuild, [switch]$PortableZip
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    $commit = (& git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[a-f0-9]{40}$') { throw 'A Git source checkout is required.' }
    if (@(& git status --porcelain --untracked-files=normal).Count) { throw 'Commit source changes before packaging an identifiable release.' }
    $shortCommit = $commit.Substring(0,12)
    $IsccPath = (Resolve-Path -LiteralPath $IsccPath).Path
    $artifactRoot = Join-Path $root 'artifacts'
    $null = New-Item -ItemType Directory -Path $artifactRoot -Force
    $receiptPath = Join-Path $artifactRoot 'installer-build.json'
    if (-not $SkipBuild) {
        if (-not $MSBuildPath) {
            $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
            $MSBuildPath = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        }
        if (-not $MSBuildPath) { throw 'Install Visual Studio C++ build tools and Qt as documented.' }
        & $MSBuildPath VideoProcessor.sln /t:Rebuild /m /p:Configuration=Release /p:Platform=x64 /nologo /v:minimal /fl "/flp:logfile=$artifactRoot\installer-build.log;verbosity=normal"
        if ($LASTEXITCODE -ne 0) { throw 'x64 Release solution rebuild failed. No installer was generated.' }
        $binaries = @(foreach ($relative in @('VideoProcessor-GUI.exe','VideoProcessorConfig.exe','VideoProcessorConfigDiscovery.dll','vprenderer/VideoProcessorVPRenderer.dll')) {
            $file = Join-Path $root "x64\Release\$relative"
            [ordered]@{ path=$relative; sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash }
        })
        [ordered]@{ commit=$commit; configuration='Release'; architecture='x64'; binaries=$binaries } |
            ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
    }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    if ($receipt.commit -ne $commit -or $receipt.configuration -ne 'Release' -or $receipt.architecture -ne 'x64' -or $receipt.binaries.Count -ne 4) {
        throw 'No successful x64 Release solution build for this commit. Rerun without -SkipBuild.'
    }
    foreach ($binary in $receipt.binaries) {
        if ((Get-FileHash -LiteralPath (Join-Path $root "x64\Release\$($binary.path)") -Algorithm SHA256).Hash -ne $binary.sha256) {
            throw "Build output changed since successful rebuild: $($binary.path)"
        }
    }
    if ((& git rev-parse HEAD).Trim() -ne $commit -or @(& git status --porcelain --untracked-files=normal).Count) {
        throw 'Sources changed during the build.'
    }
    & (Join-Path $PSScriptRoot 'package_release.ps1') -VcRedistPath $VcRedistPath
    $payload = Join-Path $artifactRoot 'release\VideoProcessor'
    $null = New-Item -ItemType Directory -Path (Join-Path $payload 'setup') -Force
    Copy-Item -LiteralPath (Join-Path $root 'packaging\installer\install-support.ps1') -Destination (Join-Path $payload 'setup')
    Copy-Item -LiteralPath (Join-Path $root 'docs\VP-0192_INSTALLER.md') -Destination (Join-Path $payload 'setup\RECOVERY.md')
    # This is only the distributable default; Inno seeds it only when absent.
    Copy-Item -LiteralPath (Join-Path $payload 'VideoProcessor.cfg.example') -Destination (Join-Path $payload 'VideoProcessor.cfg')
    $files = @(Get-ChildItem -LiteralPath $payload -Recurse -File | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($payload.Length + 1)
        $policy = if ($relative -match '^shaders\\|^VideoProcessor\.cfg(?:\.example)?$') { 'seed' } else { 'managed' }
        [ordered]@{ path=$relative.Replace('\','/'); policy=$policy; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
    [ordered]@{
        schemaVersion=1; applicationId='VideoProcessor-42D852F1-70E9-43ED-8739-D61752106D59'
        coreVersion=$CoreVersion; build=$commit; compiler='Inno Setup 6.7.3'; files=$files
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $payload 'INSTALL-MANIFEST.json') -Encoding UTF8
    $include = Join-Path $artifactRoot 'installer-payload.iss'
    $lines = @(foreach ($entry in $files) {
        $relative = $entry.path.Replace('/','\')
        $directory = Split-Path -Parent $relative
        $dest = if ($directory) { "{app}\$directory" } else { '{app}' }
        $flags = if ($entry.policy -eq 'seed') { 'onlyifdoesntexist uninsneveruninstall' } else { 'ignoreversion' }
        if ($relative -like 'setup\*') { $flags += ' uninsneveruninstall' }
        'Source: "' + (Join-Path $payload $relative) + '"; DestDir: "' + $dest + '"; Flags: ' + $flags
    })
    $lines | Set-Content -LiteralPath $include -Encoding UTF8
    $output = Join-Path $artifactRoot 'installers'
    & $IsccPath "/DPayloadRoot=$payload" "/DPayloadInclude=$include" "/DOutputRoot=$output" "/DCoreVersion=$CoreVersion" "/DBuildCommit=$shortCommit" (Join-Path $root 'packaging\installer\VideoProcessor.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
    $installer = Join-Path $output "VideoProcessor-$CoreVersion-$shortCommit-x64-Setup.exe"
    "$((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash)  $([IO.Path]::GetFileName($installer))" |
        Set-Content -LiteralPath ($installer + '.sha256') -Encoding ASCII
    if ($PortableZip) {
        & (Join-Path $PSScriptRoot 'package_release.ps1') -VcRedistPath $VcRedistPath -StageRoot (Join-Path $artifactRoot 'portable\VideoProcessor')
        $zip = Join-Path $output "VideoProcessor-$CoreVersion-$shortCommit-x64-Portable.zip"
        Compress-Archive -Path (Join-Path $artifactRoot 'portable\VideoProcessor\*') -DestinationPath $zip -Force
        "$((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash)  $([IO.Path]::GetFileName($zip))" |
            Set-Content -LiteralPath ($zip + '.sha256') -Encoding ASCII
    }
    Write-Host "Installer: $installer"
    Write-Host 'Unsigned distribution: qualify and publisher-sign before public release. See docs/VP-0192_INSTALLER.md.'
} finally { Pop-Location }
