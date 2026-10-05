$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root=Split-Path -Parent $PSScriptRoot
$fixture=Join-Path $root ('artifacts\portable-config-tests-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $fixture
$sample=Join-Path $fixture 'source.cfg'
[IO.File]::WriteAllText($sample,'# sample configuration')
$hash=(Get-FileHash -LiteralPath $sample).Hash
$cases=@('valid','missing','empty','different','nested','duplicate','active','bad-manifest')
foreach($case in $cases){
    $path=Join-Path $fixture ($case+'.zip')
    $zip=[IO.Compression.ZipFile]::Open($path,[IO.Compression.ZipArchiveMode]::Create)
    try {
        $names=@('VideoProcessor.cfg.example')
        if($case -eq 'missing'){$names=@()}
        if($case -eq 'nested'){$names=@('nested/VideoProcessor.cfg.example')}
        if($case -eq 'duplicate'){$names+= 'VideoProcessor.cfg.example'}
        if($case -eq 'active'){$names+= 'VideoProcessor.cfg'}
        foreach($name in $names){
            $writer=[IO.StreamWriter]::new($zip.CreateEntry($name).Open(),[Text.UTF8Encoding]::new($false))
            try {
                if($case -eq 'different'){$writer.Write('# wrong sample')}
                elseif($case -ne 'empty'){$writer.Write('# sample configuration')}
            } finally {$writer.Dispose()}
        }
        $record=@{path='VideoProcessor.cfg.example';policy='seed';sha256=$hash}
        if($case -eq 'bad-manifest'){$record.sha256='0'*64}
        $writer=[IO.StreamWriter]::new($zip.CreateEntry('INSTALL-MANIFEST.json').Open())
        try {$writer.Write((@{files=@($record)} | ConvertTo-Json -Depth 4))} finally {$writer.Dispose()}
    } finally {$zip.Dispose()}
    $failed=$false
    try { & (Join-Path $PSScriptRoot 'test_portable_config.ps1') -ZipPath $path -ExpectedConfigPath $sample }
    catch { $failed=$true }
    if(($case -eq 'valid' -and $failed) -or ($case -ne 'valid' -and !$failed)){throw "Portable config regression failed: $case"}
    Write-Host "PASS portable config contract: $case"
}
Write-Host "$($cases.Count) portable config contract checks passed. Fixtures: $fixture"