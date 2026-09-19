function Get-VpPeLinkerVersion {
    param([Parameter(Mandatory = $true)][string]$FilePath)
    $stream = [IO.File]::OpenRead($FilePath)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE file: $FilePath" }
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 0 -or $offset + 28 -gt $stream.Length) { throw "Invalid PE header: $FilePath" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "Invalid PE signature: $FilePath" }
        if ($reader.ReadUInt16() -ne 0x8664) { throw "Release binary is not x64: $FilePath" }
        $stream.Position = $offset + 26
        return [version]::new($reader.ReadByte(), $reader.ReadByte(), 0, 0)
    } finally { $reader.Dispose() }
}

function Get-VpPackageRuntimeRequirement {
    param($CopyPlan, $Policy, [string]$InstallerPath)
    $minimum = [version]$Policy.minimumVersion
    $buildRecords = @()
    foreach ($destination in $Policy.buildArtifacts) {
        $entry = @($CopyPlan | Where-Object { $_.RelativeDestination -eq $destination.Replace('/', '\') })
        if ($entry.Count -ne 1) { throw "Missing/duplicate runtime build artifact: $destination" }
        $recordPath = $entry[0].Source + '.runtime.json'
        if (-not (Test-Path -LiteralPath $recordPath -PathType Leaf)) {
            throw "Missing build runtime record: $recordPath. Rebuild x64 Release with Directory.Build.targets before packaging."
        }
        $record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
        if ($record.schemaVersion -ne 1 -or $record.configuration -ne 'Release' -or
            $record.architecture -ne 'x64' -or
            $record.sha256 -ne (Get-FileHash -LiteralPath $entry[0].Source -Algorithm SHA256).Hash) {
            throw "Stale or invalid runtime build record: $recordPath"
        }
        $toolset = [version]$record.toolsetVersion
        if ($toolset -gt $minimum) { $minimum = $toolset }
        $buildRecords += [ordered]@{ file = $destination; toolsetVersion = $toolset.ToString(); sha256 = $record.sha256 }
    }
    # Include shipped third-party DLLs (especially Qt), not just VP's host toolset.
    # The policy floor records the tested full runtime version for binary dependencies;
    # PE headers additionally prevent silently accepting a newer linker family.
    foreach ($entry in $CopyPlan) {
        if ($entry.RelativeDestination -eq 'prerequisites\vc_redist.x64.exe' -or
            [IO.Path]::GetExtension($entry.Source) -notin @('.exe', '.dll')) { continue }
        $linker = Get-VpPeLinkerVersion $entry.Source
        if ($linker.Major -ge 14 -and $linker -gt $minimum) { $minimum = $linker }
    }
    $installerVersion = Assert-VpRedistributable -FilePath $InstallerPath -MinimumVersion $minimum
    return [ordered]@{
        schemaVersion = 1
        architecture = 'x64'
        minimumVersion = $minimum.ToString()
        installerVersion = $installerVersion.ToString()
        installerSha256 = (Get-FileHash -LiteralPath $InstallerPath -Algorithm SHA256).Hash
        runtimeFiles = $Policy.runtimeFiles
        buildArtifacts = $buildRecords
    }
}
