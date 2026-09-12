# VP-0183: Restart VP Renderer when conversion input settings change

## Status

In Progress

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
