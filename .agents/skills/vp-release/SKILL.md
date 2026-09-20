---
name: vp-release
description: Merge tested VideoProcessor changes into the current beta and produce verified Windows installer or portable ZIP builds with pinned source identity and recorded hashes. Use for VP check-in, merge, and release packaging requests.
---

Use the user's authorized subset: merging, building, deploying and publishing are separate actions. This skill does not grant permission to install, restart playback, merge unrelated PRs or publish a GitHub Release.

## Merge

Only billslack2/videoprocessor is authoritative. Discover its latest beta and default branch on GitHub; stop if they disagree. Preserve existing checkouts. New source worktrees belong under E:\codex\videoprocessor, starting at the fetched beta tip.

Review the diff and test evidence for the exact clean feature commit. Use scripts/merge-vp.ps1 with Checkout, ExpectedCommit, ExpectedBaseCommit and Stage Prepare, plus Title and BodyFile. This pushes the branch and creates/reuses its PR. Attach every created PR to the Codex task. After the user has authorized merging and applicable checks have passed, use Stage Merge with the same pinned commits and PullRequest number. It refuses moved sources/bases and never bypasses failed or pending checks. If the base advanced, inspect the integration and rerun appropriate tests before selecting new pins; do not blindly retry with the new hash.

## Installer and ZIP

Read docs/VP-0192_INSTALLER.md and AGENTS.md in the selected checkout. Use a clean release checkout at the actual merged beta commit. If installer tooling is still in a separate draft PR, pin its exact commit and combine it only in an isolated release branch; disclose that provenance and leave the draft unmerged unless authorized. Verify that application sources match beta. Check in and push the release branch so its exact source is recoverable.

Run scripts/build-vp.ps1 with Checkout, ExpectedCommit, BetaCommit, optional InstallerToolingCommit, CoreVersion, VsInstallPath, QtRoot, VcRedistPath, IsccPath and a new OutputDirectory. Add PortableZip when requested. Pass full paths; record chosen tool inputs. It requires Inno Setup 6.7.3, performs the existing full x64 Release installer build, runs all unit tests plus installer support/identity/runtime checks, verifies payload hashes, and exports only after those checks pass. No fabricated build receipts or copying an old build under a new identity. A failed suite stops export; investigate and preserve the failing run before retrying.

The official signed VC redistributable is a build-time validator input. Current VP-0192 distributions carry verified app-local Microsoft runtime DLLs from licensed Visual Studio, not System32. Follow the selected tooling's manifest rather than mixing legacy ZIP setup rules with the app-local installer.

Repeatable workflow means pinned commits, matching host/renderer, recorded tools/dependencies and checked hashes. Do not claim byte-identical rebuilds: PE/installer timestamps and the build system have not been qualified for that guarantee. Never overwrite a previous output directory.

Real installer lifecycle checks use a disposable account or isolated QA identity, never the user's existing install registration. Follow the installer document for fresh install, upgrades, repair, rollback and data preservation checks. Record clean Windows without Visual Studio and Config Apply/OK/reopen coverage separately; helper tests are not substitutes. Report unsigned status.

Keep VP-0189 in Review unless the user asks otherwise. Update the authoritative C:\Users\bslac\vp\story-tracker checkout on main, synchronize origin/main before editing, and commit/push release evidence. Link installer, ZIP if requested, checksums and receipt. Deployment to C:\Videoprocessor\vp requires authorization, a backup and verification of both host and renderer; preserve active configuration.
