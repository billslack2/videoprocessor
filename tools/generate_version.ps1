# From https://blog.didenko.com/2013/11/version-inventory.html

Param (
 [String]$Project,
 [String]$GitRoot,
 [String]$HeaderFile="version.h",
 [String]$VerPrefix="https://github.com/billslack2/videoprocessor/commit/"
)

$ErrorActionPreference = "Stop"

Push-Location -LiteralPath $GitRoot

function Invoke-GitText {
  Param ([String[]]$GitArguments)

  $Result = & git @GitArguments 2>$null
  if ($LASTEXITCODE -ne 0) {
    return ""
  }
  return (($Result | Out-String).Trim())
}

function ConvertTo-CppString {
  Param ([String]$Value)

  if ($null -eq $Value) {
    return ""
  }
  return $Value.Replace('\', '\\').Replace('"', '\"')
}

$VerFileHead     = "`#pragma once`n`n`#include <atlstr.h>`n`n"
$VerFileTail     = "`n"
$VerDescribePre  = "static const TCHAR* VERSION_DESCRIBE=TEXT(`""
$VerDescribePost = "`");`n"

$VerBy       = (git log -n 1 --format=format:"static const TCHAR* VERSION_AUTHOR=TEXT(`\`"%an `<%ae`>`\`");%n") | Out-String
$VerUrl      = (git log -n 1 --format=format:"static const TCHAR* VERSION_URL=TEXT(`\`"$VerPrefix%H`\`");%n") | Out-String
$VerDate     = (git log -n 1 --format=format:"static const TCHAR* VERSION_DATE=TEXT(`\`"%ai`\`");%n") | Out-String
$VerDescribe = Invoke-GitText -GitArguments @("describe", "--tags")
$VerCommitShort = Invoke-GitText -GitArguments @("rev-parse", "--short=7", "HEAD")
$VerBranch = Invoke-GitText -GitArguments @("symbolic-ref", "--quiet", "--short", "HEAD")
$DefaultRemoteRef = Invoke-GitText -GitArguments @("symbolic-ref", "--quiet", "--short", "refs/remotes/origin/HEAD")
$DefaultBranch = $DefaultRemoteRef -replace '^origin/', ''

# Detached CI checkouts do not have a symbolic branch. Prefer the source
# branch supplied by common CI systems, while keeping local builds exact.
if ([String]::IsNullOrWhiteSpace($VerBranch)) {
  $BranchCandidates = @(
    $env:GITHUB_HEAD_REF,
    $env:GITHUB_REF_NAME,
    $env:BUILD_SOURCEBRANCHNAME,
    $env:CI_COMMIT_REF_NAME,
    $env:BRANCH_NAME,
    $env:GIT_BRANCH
  )
  $VerBranch = $BranchCandidates |
    Where-Object { -not [String]::IsNullOrWhiteSpace($_) } |
    Select-Object -First 1
}
# A clean detached checkout is the normal deployment form. Resolve a remote
# branch that points exactly at HEAD, preferring a beta integration branch.
# This prevents a new beta build from inheriting an unrelated historical tag
# from `git describe`.
if ([String]::IsNullOrWhiteSpace($VerBranch)) {
  $ExactRemoteBranches = @(
    (& git for-each-ref '--format=%(refname:short)' --points-at HEAD refs/remotes/origin 2>$null) |
      Where-Object {
        -not [String]::IsNullOrWhiteSpace($_) -and $_ -ne 'origin/HEAD'
      })
  $VerBranch = $ExactRemoteBranches |
    Where-Object { $_ -match '^origin/v\d+\.\d+\.\d+-beta$' } |
    Select-Object -First 1
  if ([String]::IsNullOrWhiteSpace($VerBranch)) {
    $VerBranch = $ExactRemoteBranches | Select-Object -First 1
  }
  if ([String]::IsNullOrWhiteSpace($VerBranch) -and
      -not [String]::IsNullOrWhiteSpace($DefaultRemoteRef)) {
    $DefaultHead = Invoke-GitText -GitArguments @("rev-parse", $DefaultRemoteRef)
    $Head = Invoke-GitText -GitArguments @("rev-parse", "HEAD")
    if (-not [String]::IsNullOrWhiteSpace($Head) -and $Head -eq $DefaultHead) {
      $VerBranch = $DefaultRemoteRef
    }
  }
}

# The beta integration line is a moving branch rather than a rolling tag.
# A descendant build must not inherit an unrelated historical tag (for
# example v1.1.016-beta) from git describe.
if ($DefaultBranch -match '^v\d+\.\d+\.\d+-beta$' -and
    -not [String]::IsNullOrWhiteSpace($VerCommitShort)) {
  & git merge-base --is-ancestor $DefaultRemoteRef HEAD 2>$null
  if ($LASTEXITCODE -eq 0) {
    $VerDescribe = "$DefaultBranch-$VerCommitShort"
  }
}

if (-not [String]::IsNullOrWhiteSpace($VerBranch)) {
  $VerBranch = $VerBranch.Trim()
  $VerBranch = $VerBranch -replace '^refs/heads/', ''
  $VerBranch = $VerBranch -replace '^refs/remotes/', ''
  $VerBranch = $VerBranch -replace '^origin/', ''
}

if ([String]::IsNullOrWhiteSpace($VerCommitShort)) {
  $CommitCandidates = @(
    $env:GITHUB_SHA,
    $env:BUILD_SOURCEVERSION,
    $env:CI_COMMIT_SHA
  )
  $VerCommitShort = $CommitCandidates |
    Where-Object { -not [String]::IsNullOrWhiteSpace($_) } |
    Select-Object -First 1
  if (-not [String]::IsNullOrWhiteSpace($VerCommitShort) -and
      $VerCommitShort.Length -gt 7) {
    $VerCommitShort = $VerCommitShort.Substring(0, 7)
  }
}
if ([String]::IsNullOrWhiteSpace($VerCommitShort) -and
    $VerDescribe -match '(?i)-g([0-9a-f]{7,40})(?:-dirty)?$') {
  $VerCommitShort = $Matches[1].Substring(0, 7)
}

$VerBranchLine = "static const TCHAR* VERSION_BRANCH=TEXT(`"$(ConvertTo-CppString $VerBranch)`");`n"
$VerCommitLine = "static const TCHAR* VERSION_COMMIT_SHORT=TEXT(`"$(ConvertTo-CppString $VerCommitShort)`");`n"

$VerChgs = ((git ls-files --exclude-standard -d -m -o -k) | Measure-Object -Line).Lines

if ($VerChgs -gt 0) {
  $VerDirty = "const bool VERSION_DIRTY=true;`n"
} else {
  $VerDirty = "const bool VERSION_DIRTY=false;`n"
}

$HeaderPath = Join-Path $Project $HeaderFile
$HeaderContent = "$VerFileHead$VerUrl$VerDate$VerDescribePre$VerDescribe$VerDescribePost$VerBranchLine$VerCommitLine$VerBy$VerDirty$VerFileTail"
$ExistingContent = if (Test-Path -LiteralPath $HeaderPath) {
  [IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath))
} else {
  $null
}
if ($ExistingContent -cne $HeaderContent) {
  [IO.File]::WriteAllText($HeaderPath, $HeaderContent,
    [Text.UTF8Encoding]::new($false))
}
"Written $HeaderPath as:"
""
Get-Content -LiteralPath $HeaderPath
""

Pop-Location
