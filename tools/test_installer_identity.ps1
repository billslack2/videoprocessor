$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'installer_build_identity.ps1')
$fixture=Join-Path $root ('artifacts\installer-identity-tests-'+[guid]::NewGuid().ToString('N'))
$null=New-Item -ItemType Directory -Path $fixture -Force
$script:checks=0
function Assert([bool]$Condition,[string]$Message){
 if(-not $Condition){throw $Message};$script:checks++;Write-Host "PASS $Message"
}
function Invoke-FixtureGit([string[]]$Arguments){
 & git -C $fixture @Arguments | Out-Null
 if($LASTEXITCODE -ne 0){throw 'Fixture git command failed'}
}
Invoke-FixtureGit @('init','--quiet')
Invoke-FixtureGit @('config','user.name','Installer Fixture')
Invoke-FixtureGit @('config','user.email','installer-fixture@example.invalid')
[IO.File]::WriteAllText((Join-Path $fixture '.gitignore'),'/artifacts/')
[IO.File]::WriteAllText((Join-Path $fixture 'tracked.txt'),'original')
Invoke-FixtureGit @('add','.')
Invoke-FixtureGit @('commit','--quiet','-m','fixture')
$clean=Get-VpSourceIdentity $fixture
Assert (-not $clean.dirty -and $clean.build -eq $clean.commit) 'clean build uses its exact commit'
Assert ((Get-VpInstallerBaseName '1.3.005-beta' $clean) -eq ('VideoProcessorSetup-1.3.005-beta-'+$clean.commit.Substring(0,12))) 'clean installer includes version and commit SHA'
[IO.File]::WriteAllText((Join-Path $fixture 'tracked.txt'),'edited')
$dirty=Get-VpSourceIdentity $fixture
Assert ($dirty.dirty -and $dirty.fingerprint -ne $clean.fingerprint) 'tracked edits produce a distinct source fingerprint'
Assert ((Get-VpInstallerBaseName '1.3.005-beta' $dirty) -eq ('VideoProcessorSetup-1.3.005-beta-'+$dirty.commit.Substring(0,12)+'-dirty-'+$dirty.fingerprint.Substring(0,12))) 'dirty installer names include commit SHA and source fingerprint'
[IO.File]::WriteAllText((Join-Path $fixture 'tracked.txt'),'different edit')
$changed=Get-VpSourceIdentity $fixture
Assert ($changed.build -ne $dirty.build) 'different dirty contents cannot reuse build identity'
Invoke-FixtureGit @('add','tracked.txt')
$staged=Get-VpSourceIdentity $fixture
Assert ($staged.dirty -and $staged.fingerprint -eq $changed.fingerprint) 'staged edits retain the same content fingerprint'
Invoke-FixtureGit @('commit','--quiet','-m','edited')
$committed=Get-VpSourceIdentity $fixture
Assert (-not $committed.dirty -and $committed.build -ne $staged.build) 'committing returns to clean naming with a new commit'
Assert ((Get-VpInstallerBaseName '1.3.005-beta' $committed) -ne (Get-VpInstallerBaseName '1.3.005-beta' $clean)) 'same-version clean commits have distinct installer filenames'
$extra=Join-Path $fixture 'untracked space.txt'
[IO.File]::WriteAllText($extra,'new source')
$untracked=Get-VpSourceIdentity $fixture
Assert ($untracked.dirty -and $untracked.fingerprint -ne $committed.fingerprint) 'untracked source files are included'
[IO.File]::WriteAllText($extra,'changed untracked source')
Assert ((Get-VpSourceIdentity $fixture).fingerprint -ne $untracked.fingerprint) 'untracked content changes affect the fingerprint'
Remove-Item -LiteralPath $extra
$ignored=Join-Path $fixture 'artifacts'
$null=New-Item -ItemType Directory -Path $ignored -Force
[IO.File]::WriteAllText((Join-Path $ignored 'output.exe'),'generated')
Assert ((Get-VpSourceIdentity $fixture).build -eq $committed.build) 'ignored build outputs do not dirty source identity'
Remove-Item -LiteralPath (Join-Path $fixture 'tracked.txt')
$deleted=Get-VpSourceIdentity $fixture
Assert ($deleted.dirty -and $deleted.fingerprint -ne $committed.fingerprint) 'source deletion invalidates the build identity'
# Inspect only the inventory and receipt guard AST. Do not invoke the builder,
# fabricate an installer receipt, compile an application or package a fixture.
$parseErrors=$null
$builderAst=[System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'build_installer.ps1'),[ref]$null,[ref]$parseErrors)
Assert ($null -eq $parseErrors -or @($parseErrors).Count -eq 0) 'installer builder parses for receipt contract checks'
$inventory=$builderAst.Find({param($node)
    $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left.Extent.Text -eq '$requiredBinaries'
},$true)
Assert ($null -ne $inventory) 'receipt producer and verifier declare one required binary inventory'
$requiredBinaries=@($inventory.Right.FindAll({param($node)
    $node -is [System.Management.Automation.Language.StringConstantExpressionAst]
},$true) | ForEach-Object { $_.Value })
$expectedBinaries=@('VideoProcessor-GUI.exe','VideoProcessorConfig.exe',
    'VideoProcessorConfigDiscovery.dll','vprenderer/VideoProcessorVPRenderer.dll',
    'vprenderer-beta/VideoProcessorVPRendererBeta.dll')
Assert ($requiredBinaries.Count -eq 5 -and
    @(Compare-Object $requiredBinaries $expectedBinaries).Count -eq 0 -and
    @($requiredBinaries | Select-Object -Unique).Count -eq 5) 'all five host, Config and stable/beta renderer binaries are required'
$builderSource=$builderAst.Extent.Text
Assert ($builderSource -match 'foreach\s*\(\s*\$relative\s+in\s+\$requiredBinaries\s*\)') 'receipt creation uses the shared required inventory'
$guard=$builderAst.Find({param($node)
    $node -is [System.Management.Automation.Language.IfStatementAst] -and
        $node.Extent.Text.Contains('No successful x64 Release solution build for these exact sources.')
},$true)
Assert ($null -ne $guard) 'exact-build receipt rejection guard is present'
$guardText=$guard.Clauses[0].Item1.Extent.Text
Assert ($guardText -match '\$receipt\.binaries\.Count\s+-ne\s+\$requiredBinaries\.Count') 'receipt verifier uses the same inventory count as creation'
$rejectReceipt=[scriptblock]::Create('param($receipt,$identity,$commit,$requiredBinaries) '+$guardText)
$receiptIdentity=[pscustomobject]@{fingerprint='fixture-fingerprint';dirty=$false}
$receiptCommit='a'*40
$validReceipt=[ordered]@{sourceFingerprint=$receiptIdentity.fingerprint;dirty=$false;
    commit=$receiptCommit;configuration='Release';architecture='x64';
    binaries=@($requiredBinaries | ForEach-Object { [ordered]@{path=$_;sha256='b'*64} })}
Assert (-not (& $rejectReceipt ([pscustomobject]$validReceipt) $receiptIdentity $receiptCommit $requiredBinaries)) 'fresh five-binary exact x64 Release receipt passes the real guard'
foreach($case in @('missing-beta','duplicate-host','unexpected-path','extra-binary','wrong-source','dirty','wrong-commit','debug','x86')){
    $receipt=$validReceipt | ConvertTo-Json -Depth 6 | ConvertFrom-Json
    if($case -eq 'missing-beta'){$receipt.binaries=@($receipt.binaries | Where-Object {$_.path -ne 'vprenderer-beta/VideoProcessorVPRendererBeta.dll'})}
    if($case -eq 'duplicate-host'){$receipt.binaries[-1].path='VideoProcessor-GUI.exe'}
    if($case -eq 'unexpected-path'){$receipt.binaries[-1].path='unexpected.dll'}
    if($case -eq 'extra-binary'){$receipt.binaries+= [pscustomobject]@{path='unexpected.dll';sha256='c'*64}}
    if($case -eq 'wrong-source'){$receipt.sourceFingerprint='older-source'}
    if($case -eq 'dirty'){$receipt.dirty=$true}
    if($case -eq 'wrong-commit'){$receipt.commit='c'*40}
    if($case -eq 'debug'){$receipt.configuration='Debug'}
    if($case -eq 'x86'){$receipt.architecture='x86'}
    Assert ([bool](& $rejectReceipt $receipt $receiptIdentity $receiptCommit $requiredBinaries)) "invalid build receipt remains rejected: $case"
}
Write-Host "$checks installer identity checks passed. Fixtures: $fixture"
