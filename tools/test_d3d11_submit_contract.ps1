[CmdletBinding()]
param([string] $RepositoryRoot)
$ErrorActionPreference = 'Stop'
if (-not $RepositoryRoot) { $RepositoryRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path) }
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Read-ArchiveSource($Archive, [string] $Suffix) {
    $entries = @($Archive.Entries | Where-Object { ('/' + $_.FullName.TrimStart('/')).EndsWith($Suffix) })
    if ($entries.Count -ne 1) { throw "Expected one source entry ending in $Suffix" }
    $reader = [IO.StreamReader]::new($entries[0].Open())
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}
function Get-FunctionBody([string] $Source, [string] $Name) {
    $code = [regex]::Replace($Source, '(?s)/\*.*?\*/', '')
    $code = [regex]::Replace($code, '(?m)//[^\r\n]*', '')
    $pattern = '\b' + [regex]::Escape($Name) + '\s*\([^;]*?\)\s*\{'
    $match = [regex]::Match($code, $pattern, [Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $match.Success) { throw "Missing function $Name" }
    $start = $match.Index + $match.Length
    $depth = 1
    for ($index = $start; $index -lt $code.Length; ++$index) {
        if ($code[$index] -eq '{') { ++$depth }
        if ($code[$index] -eq '}') { --$depth }
        if ($depth -eq 0) { return [regex]::Replace($code.Substring($start, $index-$start), '\s+', '') }
    }
    throw "Unclosed function $Name"
}

# These exact small backend bodies are the proof for abandoning an incompatible
# acquired repeat target without a visible Present. Any dependency implementation
# change requires reviewing this contract before enabling that optimization.
$submit = 'structpriv*p=PL_PRIV(sw);structd3d11_ctx*ctx=p->ctx;pl_tex_destroy(sw->gpu,&p->backbuffer);return!ctx->is_failed;'
$delegate = 'conststructpl_sw_fns*impl=PL_PRIV(sw);returnimpl->submit_frame(sw);'
$unwrap = 'structpriv*p=PL_PRIV(sw);IDXGISwapChain_AddRef(p->swapchain);returnp->swapchain;'
foreach ($dependency in @('libplacebo', 'libplacebo-beta')) {
    $directory = Join-Path $RepositoryRoot "3rdparty/$dependency/source"
    $archives = @(Get-ChildItem -LiteralPath $directory -Filter '*-source.zip' -File)
    if ($archives.Count -ne 1) { throw "Expected one pinned $dependency source archive" }
    $archivePath = $archives[0].FullName
    $archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
    try {
        $backend = Read-ArchiveSource $archive '/src/d3d11/swapchain.c'
        $public = Read-ArchiveSource $archive '/src/swapchain.c'
        if ((Get-FunctionBody $backend 'd3d11_sw_submit_frame') -cne $submit) {
            throw "${dependency}: D3D11 submit is no longer the verified release-only implementation"
        }
        if ((Get-FunctionBody $public 'pl_swapchain_submit_frame') -cne $delegate) {
            throw "${dependency}: public submit is no longer a direct backend delegation"
        }
        if ((Get-FunctionBody $backend 'pl_d3d11_swapchain_unwrap') -cne $unwrap) {
            throw "${dependency}: unwrap no longer has the verified AddRef/no-lock behavior"
        }
        $present = Get-FunctionBody $backend 'd3d11_sw_swap_buffers'
        if (-not $present.Contains('IDXGISwapChain_Present(p->swapchain,1,0)')) {
            throw "${dependency}: explicit swap_buffers Present implementation changed"
        }
    } finally { $archive.Dispose() }
    $hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Output "D3D11_FRAME_SUBMISSION_CONTRACT_PASS dependency=$dependency archive=$($archives[0].Name) sha256=$hash"
}
