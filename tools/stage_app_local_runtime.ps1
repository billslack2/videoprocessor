[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$PayloadRoot,
 [string[]]$RuntimeDirectories,
 [string]$DumpbinPath
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot '..\packaging\prerequisites\runtime-common.ps1')
. (Join-Path $PSScriptRoot 'runtime_packaging.ps1')
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
if(-not $DumpbinPath){
 $DumpbinPath=& $vswhere -latest -products '*' -find 'VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe' | Select-Object -Last 1
}
if(-not $DumpbinPath -or -not (Test-Path -LiteralPath $DumpbinPath)){throw 'Visual Studio dumpbin is required to verify private runtime dependencies.'}
if(-not $RuntimeDirectories){
 $RuntimeDirectories=@(& $vswhere -all -products '*' -find 'VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT\msvcp140.dll' |
   ForEach-Object {Split-Path -Parent $_})
 $RuntimeDirectories+=@(& $vswhere -all -products '*' -find 'VC\Redist\MSVC\*\x64\Microsoft.VC*.MFC\mfc140u.dll' |
   ForEach-Object {Split-Path -Parent $_})
}
$runtimeNames='^(msvcp140.*|vcruntime140.*|concrt140|vccorlib140|mfc140u|vcomp140)\.dll$'
function Get-Dependencies([string]$File){
 $lines=& $DumpbinPath /nologo /dependents $File
 if($LASTEXITCODE -ne 0){throw "Cannot inspect dependencies: $File"}
 @($lines | ForEach-Object {if($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$'){$Matches[1].ToLowerInvariant()}} | Sort-Object -Unique)
}
$requirement=Get-Content -LiteralPath (Join-Path $PayloadRoot 'prerequisites\runtime-requirement.json') -Raw | ConvertFrom-Json
$minimum=[version]$requirement.minimumVersion
$groups=@{'host'=@();'config'=@();'vprenderer'=@()}
$mfcMinimum=[version]'14.0.0.0'
foreach($file in Get-ChildItem -LiteralPath $PayloadRoot -File -Recurse){
 if($file.Extension -notin @('.exe','.dll')){continue}
 $relative=$file.FullName.Substring($PayloadRoot.TrimEnd('\').Length+1).Replace('\','/')
 if($relative -like 'prerequisites/*'){continue}
 $dependencies=@(Get-Dependencies $file.FullName | Where-Object {$_ -match $runtimeNames})
 $group=if($relative -like 'config/*'){'config'}elseif($relative -like 'vprenderer/*'){'vprenderer'}else{'host'}
 $groups[$group]+=$dependencies
 if($dependencies -contains 'mfc140u.dll'){
  $record=@($requirement.buildArtifacts | Where-Object file -eq $relative)
  $floor=if($record.Count -eq 1){[version]$record[0].toolsetVersion}else{$minimum}
  if($floor -gt $mfcMinimum){$mfcMinimum=$floor}
 }
}
$sources=@{};$entries=@()
foreach($group in @('host','config','vprenderer')){
 $pending=[Collections.Generic.Queue[string]]::new()
 foreach($name in $groups[$group]){$pending.Enqueue($name)}
 $seen=@{}
 while($pending.Count){
  $name=$pending.Dequeue()
  if($seen.ContainsKey($name)){continue};$seen[$name]=$true
  $floor=if($name -eq 'mfc140u.dll'){$mfcMinimum}else{$minimum}
  if(-not $sources.ContainsKey($name)){
   $candidates=@(foreach($directory in $RuntimeDirectories){
    if($directory -match '[\\/]debug_nonredist[\\/]|[\\/]System32(?:[\\/]|$)'){throw 'Runtime inputs must be Release redistributable files, never debug or system DLLs.'}
    $candidate=Join-Path $directory $name
    if(Test-Path -LiteralPath $candidate -PathType Leaf){
     [pscustomobject]@{file=$candidate;version=(Get-VpFileVersion $candidate)}
    }
   })
   $selected=$candidates | Where-Object version -ge $floor | Sort-Object version -Descending | Select-Object -First 1
   if(-not $selected){throw "No redistributable x64 $name meets $floor. Supply its official Visual Studio Release redist directory."}
   $null=Get-VpPeLinkerVersion $selected.file
   $signature=Get-AuthenticodeSignature -LiteralPath $selected.file
   if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '(^|, )O=Microsoft Corporation(,|$)'){
    throw "Runtime must have a valid Microsoft signature: $($selected.file)"
   }
   $sources[$name]=$selected
  }
  $source=$sources[$name]
  foreach($dependency in Get-Dependencies $source.file | Where-Object {$_ -match $runtimeNames}){$pending.Enqueue($dependency)}
  $relative=if($group -eq 'host'){$name}else{$group+'/'+$name}
  $destination=Join-Path $PayloadRoot $relative
  Copy-Item -LiteralPath $source.file -Destination $destination -Force
  $hash=(Get-FileHash -LiteralPath $source.file -Algorithm SHA256).Hash
  if((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $hash){throw "Runtime copy mismatch: $relative"}
  $entries+=[ordered]@{path=$relative;version=$source.version.ToString();minimumVersion=$floor.ToString();sha256=$hash}
 }
}
if(-not $entries.Count){throw 'No app-local VC runtime dependencies were discovered.'}
[pscustomobject]@{mode='app-local';architecture='x64';crtMinimum=$minimum.ToString();mfcMinimum=$mfcMinimum.ToString();files=$entries}

