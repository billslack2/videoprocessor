[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$')][string]$CoreVersion,
    [Parameter(Mandatory=$true)][string]$VcRedistPath,
    [Parameter(Mandatory=$true)][string]$IsccPath,
    [string]$MSBuildPath, [string[]]$RuntimeDirectories, [switch]$SkipBuild, [switch]$PortableZip
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'installer_build_identity.ps1')
Push-Location $root
try {
    $identity = Get-VpSourceIdentity $root
    $commit = $identity.commit
    $buildLabel = $identity.label
    $installerBaseName = Get-VpInstallerBaseName $CoreVersion $identity
    $numbers = ($CoreVersion -split '-', 2)[0].Split('.')
    if (@($numbers | Where-Object { [int]$_ -gt 65535 }).Count) { throw 'Version components must fit Windows version metadata (0-65535).' }
    $fileVersion = (($numbers | ForEach-Object { [int]$_ }) -join '.') + '.0'
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
        [ordered]@{ commit=$commit; sourceFingerprint=$identity.fingerprint; dirty=$identity.dirty; configuration='Release'; architecture='x64'; binaries=$binaries } |
            ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $receiptPath -Encoding UTF8
    }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    if (-not $receipt.PSObject.Properties['sourceFingerprint'] -or $receipt.sourceFingerprint -ne $identity.fingerprint -or
        $receipt.dirty -ne $identity.dirty -or $receipt.commit -ne $commit -or $receipt.configuration -ne 'Release' -or $receipt.architecture -ne 'x64' -or $receipt.binaries.Count -ne 4) {
        throw 'No successful x64 Release solution build for these exact sources. Rerun without -SkipBuild.'
    }
    foreach ($binary in $receipt.binaries) {
        if ((Get-FileHash -LiteralPath (Join-Path $root "x64\Release\$($binary.path)") -Algorithm SHA256).Hash -ne $binary.sha256) {
            throw "Build output changed since successful rebuild: $($binary.path)"
        }
    }
    $afterBuild = Get-VpSourceIdentity $root
    if ($afterBuild.build -ne $identity.build -or $afterBuild.fingerprint -ne $identity.fingerprint) {
        throw 'Sources changed during the build.'
    }
    & (Join-Path $PSScriptRoot 'package_release.ps1') -VcRedistPath $VcRedistPath
    $payload = Join-Path $artifactRoot 'release\VideoProcessor'
    $localRuntime = & (Join-Path $PSScriptRoot 'stage_app_local_runtime.ps1') -PayloadRoot $payload -RuntimeDirectories $RuntimeDirectories
    $null = New-Item -ItemType Directory -Path (Join-Path $payload 'setup') -Force
    Copy-Item -LiteralPath (Join-Path $root 'packaging\installer\install-support.ps1') -Destination (Join-Path $payload 'setup')
    Copy-Item -LiteralPath (Join-Path $root 'docs\VP-0192_INSTALLER.md') -Destination (Join-Path $payload 'setup\RECOVERY.md')
    # This is only the distributable default; Inno seeds it only when absent.
    Copy-Item -LiteralPath (Join-Path $payload 'VideoProcessor.cfg.example') -Destination (Join-Path $payload 'VideoProcessor.cfg')
    # The portable release remains the validated build input. Only application files
    # enter Inno's installed payload; setup tools are embedded with dontcopy below.
    $setupOnly = @(
        'START-HERE.txt', 'SETUP-RUNTIME.cmd', 'RELEASE-MANIFEST.json', 'RELEASE-LAYOUT.md',
        'VideoProcessor.cfg.example', 'docs/REQ-006-sdr-transfer-implementation.md',
        'docs/VP-0174-config-ui.md'
    )
    $cleanupFiles = @()
    $files = @(Get-ChildItem -LiteralPath $payload -Recurse -File | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($payload.Length + 1)
        $portablePath = $relative.Replace('\','/')
        if ($portablePath -in $setupOnly -or $portablePath -match '^(prerequisites|setup)/') {
            $cleanupFiles += [ordered]@{ path=$portablePath; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
            return
        }
        $policy = if ($relative -match '^shaders\\|^VideoProcessor\.cfg$') { 'seed' } else { 'managed' }
        [ordered]@{ path=$relative.Replace('\','/'); policy=$policy; sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    })
    [ordered]@{
        schemaVersion=1; applicationId='VideoProcessor-42D852F1-70E9-43ED-8739-D61752106D59'
        coreVersion=$CoreVersion; build=$identity.build; sourceCommit=$commit; sourceFingerprint=$identity.fingerprint; dirty=$identity.dirty; uninstallEntryPoint='Uninstall VideoProcessor.lnk'; runtimeMode='app-local'; appLocalRuntime=$localRuntime; compiler='Inno Setup 6.7.3'; files=$files; cleanupFiles=$cleanupFiles
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $payload 'INSTALL-MANIFEST.json') -Encoding UTF8
    $include = Join-Path $artifactRoot 'installer-payload.iss'
    $lines = @(foreach ($entry in $files) {
        $relative = $entry.path.Replace('/','\')
        $directory = Split-Path -Parent $relative
        $dest = if ($directory) { "{app}\$directory" } else { '{app}' }
        $flags = if ($entry.policy -eq 'seed') { 'onlyifdoesntexist uninsneveruninstall' } else { 'ignoreversion' }
        'Source: "' + (Join-Path $payload $relative) + '"; DestDir: "' + $dest + '"; Flags: ' + $flags
    })
    $lines | Set-Content -LiteralPath $include -Encoding UTF8
    $output = Join-Path $artifactRoot 'installers'
    & $IsccPath "/DPayloadRoot=$payload" "/DPayloadInclude=$include" "/DOutputRoot=$output" "/DCoreVersion=$CoreVersion" "/DBuildCommit=$buildLabel" "/DInstallerBaseName=$installerBaseName" "/DFileVersion=$fileVersion" "/DSetupIcon=$root\images\VideoProcessor.ico" (Join-Path $root 'packaging\installer\VideoProcessor.iss')
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed.' }
    $installer = Join-Path $output "$installerBaseName.exe"
    "$((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash)  $([IO.Path]::GetFileName($installer))" |
        Set-Content -LiteralPath ($installer + '.sha256') -Encoding ASCII
    $distributionFiles = @($installer, ($installer + '.sha256'))
    if ($PortableZip) {
        # Export the same self-contained payload, without installing anything.
        $portable = [IO.Path]::GetFullPath((Join-Path $artifactRoot 'portable\VideoProcessor'))
        if (-not $portable.StartsWith($artifactRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe portable stage.' }
        if (Test-Path -LiteralPath $portable) { Remove-Item -LiteralPath $portable -Recurse -Force }
        $portableManifest = Get-Content -LiteralPath (Join-Path $payload 'INSTALL-MANIFEST.json') -Raw | ConvertFrom-Json
        foreach ($entry in $portableManifest.files) {
            $source = Join-Path $payload $entry.path
            if ($entry.path -eq 'VideoProcessor.cfg') { $entry.path = 'VideoProcessor.cfg.example' }
            $destination = Join-Path $portable $entry.path
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
            Copy-Item -LiteralPath $source -Destination $destination
        }
        $portableManifest.cleanupFiles = @($portableManifest.cleanupFiles | Where-Object { $_.path -notin $portableManifest.files.path })
        $portableManifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $portable 'INSTALL-MANIFEST.json') -Encoding UTF8
        $zip = Join-Path $output "VideoProcessor-$CoreVersion-$buildLabel-x64-Portable.zip"
        Compress-Archive -Path (Join-Path $portable '*') -DestinationPath $zip -Force
        "$((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash)  $([IO.Path]::GetFileName($zip))" |
            Set-Content -LiteralPath ($zip + '.sha256') -Encoding ASCII
        $distributionFiles += @($zip, ($zip + '.sha256'))
    }
    $after = Get-VpSourceIdentity $root
    if ($after.build -ne $identity.build -or $after.fingerprint -ne $identity.fingerprint) {
        # Keep an interrupted package out of the distributable output directory.
        foreach ($file in $distributionFiles) {
            Move-Item -LiteralPath $file -Destination ($file + '.invalid-' + [guid]::NewGuid().ToString('N'))
        }
        throw 'Sources changed during packaging; the installer is invalid and must not be distributed.'
    }
    Write-Host "Installer: $installer"
    Write-Host "Source: $($identity.build)"
    Write-Host 'Unsigned distribution: qualify and publisher-sign before public release. See docs/VP-0192_INSTALLER.md.'
} finally { Pop-Location }
