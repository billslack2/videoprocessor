function Get-VpIterationTestPlan([object]$Plan,
    [ValidateSet('Qualification','Native','Focused')][string]$Lane='Qualification',
    [string[]]$Methods=@()) {
    if($Lane -eq 'Qualification') {
        if($Methods.Count){throw 'Qualification cannot select a subset of required tests.'}
        return $Plan
    }
    if($Lane -eq 'Focused' -and -not $Methods.Count){throw 'Focused iteration requires explicit -Methods from the critical regression manifest.'}
    $expected=@{}
    foreach($key in $Plan.expected.Keys) {
        $method=($key -split '::')[-1]
        if(($Lane -eq 'Native' -and $Plan.expected[$key] -eq 'native' -and -not $Methods.Count) -or $method -in $Methods) {
            if($Lane -eq 'Native' -and $Plan.expected[$key] -ne 'native'){throw "Native lane cannot run GPU method $method. Use -Lane Focused."}
            $expected[$key]=$Plan.expected[$key]
        }
    }
    foreach($method in $Methods) {
        if(-not @($expected.Keys | Where-Object {($_ -split '::')[-1] -eq $method}).Count){throw "Unknown required regression: $method"}
    }
    $filter=if($Methods.Count){($Methods | Select-Object -Unique | ForEach-Object {"FullyQualifiedName~$_"}) -join '|'}else{$Plan.nativeFilter}
    return [pscustomobject]@{expected=$expected;nativeCount=@($expected.Values | Where-Object {$_ -eq 'native'}).Count;
        actualCount=@($expected.Values | Where-Object {$_ -ne 'native'}).Count;filter=$filter;manifestHash=$Plan.manifestHash}
}

function New-VpCriticalNativeSourceList([string]$Root,[string]$Destination) {
    $manifest=Get-Content -LiteralPath (Join-Path $Root 'tools/critical_runtime_tests.json') -Raw | ConvertFrom-Json
    $sources=@($manifest.nativeSources)+@($manifest.explicitNativeMethods | ForEach-Object {$_.source})
    # Mixed MotionRendererTests methods reference this production function even
    # when only its CPU contracts execute. Keep this small support list explicit.
    $support=@('..\VideoProcessor-Lib\vprenderer\LibplaceboRenderParameters.cpp')
    foreach($source in $support) {
        if(-not (Test-Path -LiteralPath (Join-Path (Join-Path $Root 'src/VideoProcessor-Test') $source) -PathType Leaf)) {
            throw "Critical Native support source is missing: $source"
        }
    }
    $files=@('pch.cpp')+$support
    foreach($source in @($sources | Sort-Object -Unique)) {
        if($source -notmatch '^src/VideoProcessor-Test/([^/\\]+\.cpp)$') {
            throw "Critical Native source must be a local test translation unit: $source"
        }
        $file=$Matches[1]
        if(-not (Test-Path -LiteralPath (Join-Path $Root $source) -PathType Leaf)){throw "Critical Native source is missing: $source"}
        $files+=$file
    }
    $files=@($files | Sort-Object -Unique)
    $escaped=[Security.SecurityElement]::Escape(($files -join ';'))
    $xml='<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup><VPCriticalNativeCompileFiles>'+$escaped+'</VPCriticalNativeCompileFiles><VPCriticalNativeCompileCount>'+$files.Count+'</VPCriticalNativeCompileCount></PropertyGroup></Project>'
    $xml | Set-Content -LiteralPath $Destination -Encoding UTF8
    return [pscustomobject]@{path=[IO.Path]::GetFullPath($Destination);files=$files;count=$files.Count}
}
function Get-VpIterationOutputInventory([string]$Root,[string]$Lane) {
    $paths=@('x64/Release/VideoProcessor-Lib.lib')
    if($Lane -eq 'Native') {
        $paths+=@('x64/Release/tests-critical-native/VideoProcessor-CriticalNativeTests.dll','x64/Release/tests-critical-native/libplacebo-360.dll')
    } elseif($Lane -eq 'Focused') {
        $paths+=@('x64/Release/VideoProcessor-GUI.exe','x64/Release/VideoProcessor-Test.dll','x64/Release/libplacebo-360.dll',
            'x64/Release/vprenderer/VideoProcessorVPRenderer.dll','x64/Release/vprenderer/libplacebo-360.dll',
            'x64/Release/vprenderer-beta/VideoProcessorVPRendererBeta.dll','x64/Release/vprenderer-beta/libplacebo-388.dll')
    } else {throw 'Only partial iteration lanes use this inventory.'}
    return @(foreach($relative in $paths) {
        $path=Join-Path $Root $relative
        if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Iteration output missing: $relative"}
        [pscustomobject]@{path=$relative;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    })
}
