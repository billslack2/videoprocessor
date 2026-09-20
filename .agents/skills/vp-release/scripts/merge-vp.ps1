[CmdletBinding()]
param(
 [Parameter(Mandatory)][string]$Checkout,
 [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{40}$')][string]$ExpectedCommit,
 [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{40}$')][string]$ExpectedBaseCommit,
 [Parameter(Mandatory)][ValidateSet('Prepare','Merge')][string]$Stage,
 [string]$Title, [string]$BodyFile, [int]$PullRequest
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Invoke-Checked([string]$Exe, [string[]]$Arguments) {
 $result = & $Exe @Arguments
 if ($LASTEXITCODE -ne 0) { throw "$Exe failed ($LASTEXITCODE): $($Arguments -join ' ')" }
 return $result
}
$repo = 'billslack2/videoprocessor'
$remote = (Invoke-Checked git @('-C',$Checkout,'remote','get-url','origin')) -join ''
if ($remote -notmatch '^(https://github.com/|git@github.com:)billslack2/videoprocessor(?:\.git)?$') { throw 'Unexpected origin.' }
$head = (Invoke-Checked git @('-C',$Checkout,'rev-parse','HEAD')) -join ''
if ($head -ne $ExpectedCommit) { throw 'HEAD differs from the reviewed/tested commit.' }
if (Invoke-Checked git @('-C',$Checkout,'status','--porcelain')) { throw 'Checkout must be clean.' }
$branch = (Invoke-Checked git @('-C',$Checkout,'branch','--show-current')) -join ''
if ($branch -notlike 'codex/*') { throw 'Expected a named codex feature branch.' }
$base = (Invoke-Checked gh @('repo','view',$repo,'--json','defaultBranchRef','--jq','.defaultBranchRef.name')) -join ''
$branches = @(Invoke-Checked gh @('api',"repos/$repo/branches?per_page=100",'--paginate','--jq','.[].name'))
$latest = $branches | Where-Object { $_ -match '^v\d+\.\d+\.\d+-beta$' } | Sort-Object { [version]($_ -replace '^v|\-beta$','') } -Descending | Select-Object -First 1
if ($base -ne $latest) { throw 'Default and latest beta disagree; investigate before merging.' }
Invoke-Checked git @('-C',$Checkout,'fetch','origin',"+refs/heads/${base}:refs/remotes/origin/$base") | Out-Host
$baseSha = (Invoke-Checked git @('-C',$Checkout,'rev-parse',"origin/$base")) -join ''
if ($baseSha -ne $ExpectedBaseCommit) { throw 'Integration base advanced; review and retest the new combination.' }
Invoke-Checked git @('-C',$Checkout,'merge-base','--is-ancestor',$ExpectedBaseCommit,$ExpectedCommit) | Out-Host
if ($Stage -eq 'Prepare') {
 if (-not $Title -or -not (Test-Path -LiteralPath $BodyFile -PathType Leaf)) { throw 'Title and reviewed body file required.' }
 Invoke-Checked git @('-C',$Checkout,'push','--set-upstream','origin',$branch) | Out-Host
 $existing = (Invoke-Checked gh @('pr','list','--repo',$repo,'--head',$branch,'--base',$base,'--state','open','--json','url')) -join "`n" | ConvertFrom-Json
 if (@($existing).Count -gt 1) { throw 'Multiple matching PRs.' }
 if (@($existing).Count -eq 1) { $existing[0].url } else {
  Invoke-Checked gh @('pr','create','--repo',$repo,'--base',$base,'--head',$branch,'--title',$Title,'--body-file',$BodyFile)
 }
} else {
 if (-not $PullRequest) { throw 'Exact PR number required.' }
 $pr = (Invoke-Checked gh @('pr','view',"$PullRequest",'--repo',$repo,'--json','headRefOid,headRefName,baseRefName,isDraft,state,statusCheckRollup')) -join "`n" | ConvertFrom-Json
 if ($pr.headRefOid -ne $ExpectedCommit -or $pr.headRefName -ne $branch -or $pr.baseRefName -ne $base -or $pr.isDraft -or $pr.state -ne 'OPEN') { throw 'PR identity/state mismatch.' }
 foreach ($check in $pr.statusCheckRollup) {
  if (($check.PSObject.Properties['status'] -and $check.status -ne 'COMPLETED') -or
      ($check.PSObject.Properties['conclusion'] -and $check.conclusion -notin @('SUCCESS','NEUTRAL','SKIPPED')) -or
      ($check.PSObject.Properties['state'] -and $check.state -notin @('SUCCESS','NEUTRAL','SKIPPED'))) { throw 'PR checks are incomplete or unsuccessful.' }
 }
 Invoke-Checked gh @('pr','merge',"$PullRequest",'--repo',$repo,'--merge','--match-head-commit',$ExpectedCommit) | Out-Host
 Invoke-Checked gh @('pr','view',"$PullRequest",'--repo',$repo,'--json','url,state,mergeCommit')
}
