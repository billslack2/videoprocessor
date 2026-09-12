# VP-0183: Restart VP Renderer when conversion input settings change

## Status

Done

2026-09-12: User authorized fixing confirmed live conversion failure.

## Evidence and scope

Latest vp.log at 14:01:03 publishes vp_conversion=1 active_conversion=1 but
classifies vprenderer.input_processing as Apply rendering live. The existing
renderer remains P210 because its constructor-owned conversion value and
formatter are not recreated. Earlier general toggles correctly restarted but
were overridden by the explicit VP NONE value at that time.

Treat canonical/legacy VP input-policy sections as renderer-restart settings,
not rendering profiles. Preserve general/backend override precedence. Shared
directshow.conversion formatter settings also require restart for VP Renderer.
Retain live application for actual rendering/scaling/viewport profiles.

## Readiness and validation

Remote default/beta discovered as v1.3.005-beta at 94d2efe2. User standing
instructions authorize that base. Clean worktree E:\codex\videoprocessor\
vp-conversion-live-apply, branch codex/vp-conversion-live-apply.
Add regression coverage for conversion on/off/removal and aliases, mixed
profile/input edits and shared formatter settings. Build x64 Release and run
native tests. No change to pixel kernels or active configuration. No deployment
or merge requested for this fix yet.

## Tracker audit

201 canonical files/index rows agree; no duplicates or missing IDs; registry
max 0182, next 0183. Existing unrelated capitalization is preserved.

## Implementation and validation

Commit 3b5b6cfe101382ce1ded3a6db9ca73313da4cf3f; draft PR
https://github.com/billslack2/videoprocessor/pull/92 against v1.3.005-beta.
Canonical/legacy VP input sections and legacy renderer-root input keys now
require restart. Shared directshow.conversion settings conservatively restart
either backend; other DirectShow-only settings still stay save-only under VP.
Pixel kernels and config precedence are unchanged.

x64 Release build passed, zero errors (existing warnings). All 1,147 native
tests passed; focused UI tests for General input Apply preserving overrides and
DirectShow-only effects not restarting Alpha passed. Change classification is
value-independent and covers on/off/add/remove via the same changed key.
Logs: conversion-fix-build.log, conversion-fix-native.log and corresponding TRX
in source worktree. Source diff check passed.

Pending review/merge and deployment; active config and installed binaries were
not changed. Hardware acceptance: toggle VP input NONE -> V210_TO_P010 -> NONE,
verify Restart renderer and P010/P210 ingress after each Apply; repeat with
inherited General conversion. No live hardware toggle test has been performed.
## Deployment 2026-09-12

User requested deployment for testing. Deployed commit
3b5b6cfe101382ce1ded3a6db9ca73313da4cf3f from successful x64 Release build
(conversion-fix-deploy-build.log, zero errors). Runtime EXE and renderer DLL
replaced together, plus config editor using the corrected shared apply policy.
All three SHA256 hashes verified against build outputs. VP/editor were closed;
not launched. Active VideoProcessor.cfg hash verified unchanged.
Backup: C:\Videoprocessor\vp\backup-before-vp0183-20260912-141045.
Deployment manifest there records old/new hashes and config hash.
EXE: 9A5F4A4354661635BAEB35F5BA54C5E0A337DB12B0497EE447209C17D9F1A524.
Renderer: 0B67C340500A8467CA237FCBFA7CD82C3CC056F8D0CAFDBD0F465C29DC38E290.
Editor: 442BFF79B76144D29478F867A5CB91B4265CEA4142545167C1C740D294737852.
Review remains pending user toggle test and merge; previous not-deployed note
is superseded by this deployment.

## Acceptance and merge

2026-09-12: User tested the deployed fix, reported "looks good", and authorized
merge. PR #92 merged into v1.3.005-beta at 18:22:40 UTC, merge commit
455d919cf07065c89b6e34cfee27e1431e25603b. Fetched remote beta and verified
implementation 3b5b6cfe is included. Release build and all 1,147 native tests
passed, with focused UI checks and verified deployment recorded above.
Story is Done; earlier pending notes are superseded. Installed tested binaries
and active configuration remain unchanged by the merge.