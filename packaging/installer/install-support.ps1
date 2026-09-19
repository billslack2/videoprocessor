[CmdletBinding()]
param(
    [ValidateSet('Check','Prepare','Verify','Commit','Restore','Finalize')][string]$Action,
    [string]$InstallRoot, [string]$PayloadManifest, [string]$ResultPath,
    [string]$BackupDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-VpSafePath([string]$Root, [string]$Relative) {
    if (-not $Relative -or $Relative -match '(^|[\\/])\.\.?([\\/]|$)|[:*?"<>|]' -or
        [IO.Path]::IsPathRooted($Relative)) { throw "Unsafe package path: $Relative" }
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    $file = [IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
    if (-not $file.StartsWith($rootPath + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escapes the installation: $Relative"
    }
    $candidate = $file
    while ($candidate) {
        if ((Test-Path -LiteralPath $candidate) -and
            ((Get-Item -LiteralPath $candidate -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Setup cannot operate through a junction or symbolic link: $candidate"
        }
        $candidate = Split-Path -Parent $candidate
    }
    return $file
}
function Test-VpOperatorFile([string]$Relative) {
    return $Relative -match '^(logs|shaders|luts|profiles)([\\/]|$)|\.(cfg|ini|state|cube|hlsl|glsl|bak|log)$|VideoProcessorShaderCache\.bin$'
}
function Read-VpInstallManifest([string]$File) {
    $manifest = Get-Content -LiteralPath $File -Raw | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 1 -or -not $manifest.files -or
        $manifest.applicationId -ne 'VideoProcessor-42D852F1-70E9-43ED-8739-D61752106D59') {
        throw "Unsupported installation manifest: $File"
    }
    $seen = @{}
    foreach ($entry in $manifest.files) {
        $relative = ([string]$entry.path).Replace('/', '\')
        $null = Get-VpSafePath $InstallRoot $relative
        if ($seen.ContainsKey($relative) -or $entry.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
            $entry.policy -notin @('managed','seed')) { throw "Invalid payload entry: $relative" }
        if ($entry.policy -eq 'managed' -and (Test-VpOperatorFile $relative)) {
            throw "Operator data cannot be managed by setup: $relative"
        }
        $seen[$relative] = $true
    }
    if ($manifest.PSObject.Properties['cleanupFiles']) {
        foreach ($entry in $manifest.cleanupFiles) {
            $relative = ([string]$entry.path).Replace('/', '\')
            $null = Get-VpSafePath $InstallRoot $relative
            if ($seen.ContainsKey($relative) -or $entry.sha256 -notmatch '^[0-9a-fA-F]{64}$' -or
                (Test-VpOperatorFile $relative)) { throw "Invalid cleanup entry: $relative" }
            $seen[$relative] = $true
        }
    }
    return $manifest
}
function Assert-VpClosed {
    $running = @(Get-Process -Name @('VideoProcessor','VideoProcessor-GUI','VideoProcessorConfig') -ErrorAction SilentlyContinue)
    if ($running.Count) {
        throw ('Save your work and close VideoProcessor and Config, including its tray icon, then retry. Running: ' +
            (($running | ForEach-Object { "$($_.ProcessName) (PID $($_.Id))" }) -join ', '))
    }
}
function Assert-VpWritable([string]$Directory) {
    $null = New-Item -ItemType Directory -Path $Directory -Force
    $probe = Join-Path $Directory ('.vp-write-test-' + [guid]::NewGuid().ToString('N'))
    try {
        $stream = [IO.File]::Open($probe, 'CreateNew', 'ReadWrite', 'None')
        $stream.Dispose()
    } finally { if (Test-Path -LiteralPath $probe) { Remove-Item -LiteralPath $probe } }
}
function Get-VpPlan($Manifest) {
    $current = @{}
    foreach ($entry in $Manifest.files) { if ($entry.policy -eq 'managed') { $current[$entry.path.Replace('/', '\')] = $entry.sha256 } }
    $cleanup = @{}
    if ($Manifest.PSObject.Properties['cleanupFiles']) {
        foreach ($entry in $Manifest.cleanupFiles) { $cleanup[$entry.path.Replace('/', '\')] = $entry.sha256 }
    }
    $previous = @{}
    $installed = Get-VpSafePath $InstallRoot 'INSTALL-MANIFEST.json'
    if (Test-Path -LiteralPath $installed) {
        foreach ($entry in (Read-VpInstallManifest $installed).files) {
            $relative = $entry.path.Replace('/', '\')
            if ($entry.policy -eq 'managed' -or $cleanup.ContainsKey($relative)) { $previous[$relative] = $entry.sha256 }
        }
    } else {
        # ZIP adoption: an explicit release inventory establishes ownership, never a DLL glob.
        $legacy = Get-VpSafePath $InstallRoot 'RELEASE-MANIFEST.json'
        if (Test-Path -LiteralPath $legacy) {
            $inventory = Get-Content -LiteralPath $legacy -Raw | ConvertFrom-Json
            if ($inventory.schemaVersion -ne 1 -or $inventory.layoutVersion -ne 'VP-0107') {
                throw 'Unrecognized ZIP release inventory. Resolve its ownership before retrying.'
            }
            # This generated inventory itself is retired after adoption. Textual
            # ZIP extras have no historical hashes: delete only known package bytes.
            if ($cleanup.ContainsKey('RELEASE-MANIFEST.json')) {
                $previous['RELEASE-MANIFEST.json'] = (Get-FileHash -LiteralPath $legacy -Algorithm SHA256).Hash
            }
            foreach ($relative in $cleanup.Keys) {
                $file = Get-VpSafePath $InstallRoot $relative
                if ((Test-Path -LiteralPath $file -PathType Leaf) -and
                    (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -eq $cleanup[$relative]) {
                    $previous[$relative] = $cleanup[$relative]
                }
            }
            # Generated runtime metadata changes with every binary build. Recognize
            # its exact schema and verify its old payload/installer hashes before
            # treating it as an owned ZIP artifact rather than an arbitrary JSON.
            $runtimeRelative = 'prerequisites\runtime-requirement.json'
            $runtimeFile = Get-VpSafePath $InstallRoot $runtimeRelative
            if ($cleanup.ContainsKey($runtimeRelative) -and $inventory.PSObject.Properties['generatedFiles'] -and
                'prerequisites/runtime-requirement.json' -in $inventory.generatedFiles -and
                (Test-Path -LiteralPath $runtimeFile -PathType Leaf)) {
                try {
                    $runtime = Get-Content -LiteralPath $runtimeFile -Raw | ConvertFrom-Json
                    $properties = @('schemaVersion','architecture','minimumVersion','installerVersion','installerSha256','runtimeFiles','buildArtifacts')
                    $recognized = $runtime.schemaVersion -eq 1 -and $runtime.architecture -eq 'x64' -and
                        $runtime.buildArtifacts.Count -eq 4 -and
                        @($runtime.PSObject.Properties.Name | Where-Object { $_ -notin $properties }).Count -eq 0
                    $redist = Get-VpSafePath $InstallRoot 'prerequisites\vc_redist.x64.exe'
                    $recognized = $recognized -and (Get-FileHash -LiteralPath $redist -Algorithm SHA256).Hash -eq $runtime.installerSha256
                    foreach ($binary in $runtime.buildArtifacts) {
                        $file = Get-VpSafePath $InstallRoot $binary.file
                        if ((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $binary.sha256) { $recognized = $false }
                    }
                    if ($recognized) { $previous[$runtimeRelative] = (Get-FileHash -LiteralPath $runtimeFile -Algorithm SHA256).Hash }
                } catch { Write-Verbose "Preserving unrecognized ZIP runtime metadata: $($_.Exception.Message)" }
            }
            foreach ($entry in $inventory.files) {
                $relative = ([string]$entry.destination).Replace('/', '\')
                $file = Get-VpSafePath $InstallRoot $relative
                # Without historical hashes, retire only explicitly inventoried binaries.
                if ($relative -match '\.(dll|exe)$' -and -not (Test-VpOperatorFile $relative) -and
                    (Test-Path -LiteralPath $file -PathType Leaf)) {
                    $previous[$relative] = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
                }
            }
        }
    }
    $obsolete = @(foreach ($relative in $previous.Keys) {
        if ($current.ContainsKey($relative)) { continue }
        $file = Get-VpSafePath $InstallRoot $relative
        if ((Test-Path -LiteralPath $file -PathType Leaf) -and
            (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $previous[$relative]) {
            # A modified setup-only document/example belongs to the operator now.
            # A modified obsolete binary still blocks, because it can shadow DLLs.
            if ($cleanup.ContainsKey($relative) -and $relative -notmatch '\.(dll|exe)$') { continue }
            throw "Previously managed file has been modified: $relative. Preserve it outside this folder before retrying."
        }
        $relative
    })
    # Unknown private DLLs can shadow the selected build or the system runtime.
    foreach ($relativeDirectory in @('', 'config', 'vprenderer')) {
        $directory = if ($relativeDirectory) { Get-VpSafePath $InstallRoot $relativeDirectory } else { $InstallRoot }
        if (-not (Test-Path -LiteralPath $directory)) { continue }
        $dlls = if ($relativeDirectory -eq 'config') {
            @(Get-ChildItem -LiteralPath $directory -Filter '*.dll' -File -Recurse)
        } else { @(Get-ChildItem -LiteralPath $directory -Filter '*.dll' -File) }
        foreach ($dll in $dlls) {
            $relative = $dll.FullName.Substring($InstallRoot.TrimEnd('\').Length + 1)
            $null = Get-VpSafePath $InstallRoot $relative
            if (-not $current.ContainsKey($relative) -and -not $previous.ContainsKey($relative)) {
                throw "Unowned private DLL conflict: $relative. Back it up outside this installation, then retry. No files were replaced."
            }
        }
    }
    return [pscustomobject]@{ current=$current; obsolete=$obsolete }
}
function Write-VpJsonAtomic($Value, [string]$File) {
    $temporary = $File + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    [IO.File]::WriteAllText($temporary, ($Value | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($true))
    if (Test-Path -LiteralPath $File) { [IO.File]::Replace($temporary, $File, [NullString]::Value) }
    else { [IO.File]::Move($temporary, $File) }
}
function Restore-VpBackup([string]$Directory) {
    Assert-VpClosed
    $journalPath = Get-VpSafePath $Directory 'transaction.json'
    $journal = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json
    if ($journal.schemaVersion -ne 1 -or $journal.installRoot -ine $InstallRoot) { throw 'Backup belongs to another installation.' }
    # Validate every path and backup hash before the first write.
    foreach ($entry in $journal.files) {
        if (Test-VpOperatorFile $entry.path) { throw 'Refusing to restore operator data.' }
        $null = Get-VpSafePath $InstallRoot $entry.path
        $saved = Get-VpSafePath (Join-Path $Directory 'files') $entry.path
        if ($entry.existed -and (Get-FileHash -LiteralPath $saved -Algorithm SHA256).Hash -ne $entry.sha256) {
            throw "Backup failed verification: $($entry.path)"
        }
    }
    foreach ($entry in $journal.files) {
        $destination = Get-VpSafePath $InstallRoot $entry.path
        if ($entry.existed) {
            $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force
            Copy-Item -LiteralPath (Join-Path (Join-Path $Directory 'files') $entry.path) -Destination $destination -Force
        } elseif (Test-Path -LiteralPath $destination -PathType Leaf) { Remove-Item -LiteralPath $destination -Force }
    }
    $journal.status = 'restored'
    Write-VpJsonAtomic $journal $journalPath
}
function Remove-VpEmptyDirectories([string]$Directory) {
    # No recursive delete or traversal through links. Remove only empty parents.
    if (-not (Test-Path -LiteralPath $Directory -PathType Container)) { return }
    $children = @(Get-ChildItem -LiteralPath $Directory -Force)
    foreach ($child in $children) {
        if ($child.PSIsContainer -and -not ($child.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            Remove-VpEmptyDirectories $child.FullName
        }
    }
    if (@(Get-ChildItem -LiteralPath $Directory -Force).Count -eq 0) { [IO.Directory]::Delete($Directory) }
}
function Remove-VpCompletedBackup([string]$Directory) {
    $journalPath = Get-VpSafePath $Directory 'transaction.json'
    if (-not (Test-Path -LiteralPath $journalPath -PathType Leaf)) { return }
    $journal = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json
    if ($journal.schemaVersion -ne 1 -or $journal.installRoot -ine $InstallRoot -or
        $journal.status -notin @('complete','restored')) { return }
    $owned = @{ $journalPath = $true }
    foreach ($entry in $journal.files) {
        if (Test-VpOperatorFile $entry.path) { throw 'Refusing to prune a backup claiming operator data.' }
        $saved = Get-VpSafePath (Join-Path $Directory 'files') $entry.path
        if ($entry.existed) {
            if (-not (Test-Path -LiteralPath $saved -PathType Leaf) -or
                (Get-FileHash -LiteralPath $saved -Algorithm SHA256).Hash -ne $entry.sha256) { return }
            $owned[$saved] = $true
        }
    }
    # Check the entire transaction first. Unknown files or links retain the whole
    # backup, not just the unknown file, so manual recovery stays understandable.
    $pending = [Collections.Generic.Queue[string]]::new()
    $pending.Enqueue($Directory)
    while ($pending.Count) {
        foreach ($child in Get-ChildItem -LiteralPath $pending.Dequeue() -Force) {
            if ($child.Attributes -band [IO.FileAttributes]::ReparsePoint) { return }
            if ($child.PSIsContainer) { $pending.Enqueue($child.FullName) }
            elseif (-not $owned.ContainsKey($child.FullName)) { return }
        }
    }
    foreach ($file in $owned.Keys) { if ($file -ne $journalPath) { Remove-Item -LiteralPath $file -Force } }
    Remove-Item -LiteralPath $journalPath -Force
    Remove-VpEmptyDirectories $Directory
}
function Invoke-VpInstallAction {
    $script:InstallRoot = [IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
    if ($InstallRoot -eq [IO.Path]::GetPathRoot($InstallRoot).TrimEnd('\')) { throw 'Select a dedicated VideoProcessor folder, not a drive root.' }
    $null = Get-VpSafePath $InstallRoot 'INSTALL-MANIFEST.json'
    $history = Get-VpSafePath $InstallRoot '.vp-installer-backups'
    if ($Action -eq 'Restore') {
        if (-not $BackupDirectory) { throw 'Supply the backup directory shown in the installation log.' }
        $backup = [IO.Path]::GetFullPath($BackupDirectory)
        if (-not $backup.StartsWith($history + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Backup must be inside this installation history.' }
        Restore-VpBackup $backup
        return 'Previous application files restored. Settings/state preserved. Rerun the matching installer to repair shortcuts and registration.'
    }
    if ($Action -eq 'Finalize') {
        $backup = [IO.Path]::GetFullPath($BackupDirectory)
        if (-not $backup.StartsWith($history + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid transaction directory.' }
        $journal = Get-Content -LiteralPath (Get-VpSafePath $backup 'transaction.json') -Raw | ConvertFrom-Json
        if ($journal.installRoot -ine $InstallRoot -or $journal.status -ne 'complete') { throw 'Setup has not committed this transaction.' }
        foreach ($directory in Get-ChildItem -LiteralPath $history -Directory) {
            $safeDirectory = Get-VpSafePath $history $directory.Name
            Remove-VpCompletedBackup $safeDirectory
        }
        Remove-VpEmptyDirectories $history
        foreach ($relative in @('prerequisites','setup')) {
            Remove-VpEmptyDirectories (Get-VpSafePath $InstallRoot $relative)
        }
        return 'Completed recovery backups and empty setup-only directories cleaned; unrecognized or modified files retained.'
    }
    $manifest = Read-VpInstallManifest $PayloadManifest
    if ($Action -in @('Check','Prepare')) {
        $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
        if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            throw 'Run this per-user installer normally, not as administrator, so permissions match ordinary VP operation.'
        }
        Assert-VpClosed
        # Recover an interrupted attempt before evaluating a new package.
        if (Test-Path -LiteralPath $history) {
            foreach ($directory in Get-ChildItem -LiteralPath $history -Directory) {
                $journalPath = Get-VpSafePath $history ($directory.Name + '\transaction.json')
                if (Test-Path -LiteralPath $journalPath) {
                    $journal = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json
                    if ($journal.status -eq 'pending') { Restore-VpBackup $directory.FullName }
                }
            }
        }
        $plan = Get-VpPlan $manifest
        foreach ($directory in @($InstallRoot, (Get-VpSafePath $InstallRoot 'logs'), (Get-VpSafePath $InstallRoot 'vprenderer'))) {
            Assert-VpWritable $directory
        }
        foreach ($entry in $manifest.files) { Assert-VpWritable (Split-Path -Parent (Get-VpSafePath $InstallRoot $entry.path)) }
        foreach ($relative in @('VideoProcessor.cfg','VideoProcessor.state') + @($plan.current.Keys) + @($plan.obsolete)) {
            $file = Get-VpSafePath $InstallRoot $relative
            if (Test-Path -LiteralPath $file -PathType Leaf) {
                $stream = [IO.File]::Open($file, 'Open', 'ReadWrite', 'None'); $stream.Dispose()
            }
        }
        if ($Action -eq 'Check') { return 'Location, ownership, running applications, and write permissions checked.' }
        $backup = Join-Path $history ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N'))
        $null = New-Item -ItemType Directory -Path (Join-Path $backup 'files') -Force
        $entries = @(foreach ($relative in (@($plan.current.Keys) + @($plan.obsolete) + @('INSTALL-MANIFEST.json') | Sort-Object -Unique)) {
            $file = Get-VpSafePath $InstallRoot $relative
            $existed = Test-Path -LiteralPath $file -PathType Leaf
            $hash = ''
            if ($existed) {
                $saved = Get-VpSafePath (Join-Path $backup 'files') $relative
                $null = New-Item -ItemType Directory -Path (Split-Path -Parent $saved) -Force
                Copy-Item -LiteralPath $file -Destination $saved
                $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
                if ((Get-FileHash -LiteralPath $saved -Algorithm SHA256).Hash -ne $hash) { throw "Backup verification failed: $relative" }
            }
            [ordered]@{ path=$relative; existed=$existed; sha256=$hash }
        })
        # Publish the journal only after the entire backup is verified, then mutate.
        $journal = [ordered]@{ schemaVersion=1; installRoot=$InstallRoot; status='pending'; build=$manifest.build; files=$entries }
        Write-VpJsonAtomic $journal (Join-Path $backup 'transaction.json')
        try {
            foreach ($relative in $plan.obsolete) {
                $file = Get-VpSafePath $InstallRoot $relative
                if (Test-Path -LiteralPath $file -PathType Leaf) { Remove-Item -LiteralPath $file -Force }
            }
        } catch {
            Restore-VpBackup $backup
            throw
        }
        return $backup
    }
    if ($Action -in @('Verify','Commit')) {
        foreach ($entry in $manifest.files) {
            $file = Get-VpSafePath $InstallRoot $entry.path
            if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Installed file is missing: $($entry.path)" }
            if ($entry.policy -eq 'managed' -and (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash -ne $entry.sha256) {
                throw "Installed file does not match the selected build: $($entry.path)"
            }
        }
        if ($Action -eq 'Commit') {
            $backup = [IO.Path]::GetFullPath($BackupDirectory)
            if (-not $backup.StartsWith($history + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid transaction directory.' }
            $journalPath = Get-VpSafePath $backup 'transaction.json'
            $journal = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json
            Copy-Item -LiteralPath $PayloadManifest -Destination (Get-VpSafePath $InstallRoot 'INSTALL-MANIFEST.json') -Force
            $journal.status = 'complete'
            Write-VpJsonAtomic $journal $journalPath
        }
        return "Verified installed build $($manifest.build)."
    }
    throw 'An installer action is required.'
}
if ($MyInvocation.InvocationName -ne '.') {
    try {
        $message = Invoke-VpInstallAction
        if ($ResultPath) { [IO.File]::WriteAllText($ResultPath, [string]$message) }
        Write-Output $message
        exit 0
    } catch {
        $message = $_.Exception.Message
        if ($ResultPath) { [IO.File]::WriteAllText($ResultPath, $message) }
        Write-Error $message -ErrorAction Continue
        exit 1
    }
}
