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
Write-Host "$checks installer identity checks passed. Fixtures: $fixture"
