# VP-0188: Preserve configuration scope and avoid source-event disk reloads

## Status

In Progress — implementation requested 2026-09-15. Source branch
`codex/vp-0188-config-scope-reloads`, worktree
`E:\codex\videoprocessor\assess-config-feedback-20260915`, based on current
`origin/v1.3.005-beta` at `455d919c`.

## Problem

1. The configuration editor writes shared `switch_refresh_rate` to [general],
   while the legacy renderer validator warns that this generated key belongs in
   [vpvr.general]. A clean generated configuration must not warn about itself.
2. Opening or saving a DirectShow-only input policy promotes it to [general].
   General video conversion has no unset choice. VP Renderer also falls back to
   DirectShow, allowing a backend-specific choice to affect another backend.
   The same migration covers container color space, HDR color space/luminance.
3. A renderer source-state update loads VideoProcessor.cfg to inspect its format
   and loads it again to resolve settings. HDR metadata withdrawal/restoration
   can cause four reads per blip despite an unchanged file and continuous signal.

## Scope and intended behavior

- Keep refresh-rate shared policy canonical in [general]; align warning and
  compatibility behavior, preserving genuine legacy policy warnings.
- Preserve explicitly scoped input settings through load/save/reopen. Allow the
  shared conversion setting to be unset. Backend defaults resolve from their own
  section, shared General/command-line values, then defaults, without a DirectShow
  value leaking into VP Renderer. Document the compatibility change.
- Resolve automatic source/metadata updates against the renderer's accepted
  configuration snapshot. Explicit configuration application refreshes the
  snapshot; manual profile selection uses accepted configuration consistently.
- Add focused diagnostic evidence for configuration snapshot generation/read
  versus source-state reuse, without per-frame log noise.
- Preserve live metadata propagation, automatic profile rules, explicit reloads,
  and rollback when a configuration change is rejected.

## Acceptance and tests

1. Fresh editor config and save/reopen do not emit the shared refresh warning;
   legacy renderer-only misplaced settings still warn appropriately.
2. DirectShow-only input settings remain DirectShow-only after save/reopen;
   General can be unset, Disabled remains an explicit override, and VP Renderer
   inherits only shared settings when its own setting is unset. Cover mixed and
   legacy renderer aliases and all four input-policy keys.
3. Metadata withdrawal/restoration and automatic profile evaluation perform no
   extra configuration disk reads after snapshot acceptance. Profile shortcuts
   and explicit saved configuration application continue to work; a rejected
   candidate does not replace accepted settings.
4. Execute focused unit tests, full relevant core/config-editor suites, and a
   successful x64 Release build; record commands and results here.
5. Automate fresh-file editor/runtime checks where possible. Before hardware
   testing, ask the user to confirm an active capture signal. Capture baseline,
   repeated HDR metadata events, profile changes, and a saved config edit with
   timestamped logs; verify read/reuse counts and lack of unwanted resets.
6. If source events cannot be generated automatically, provide a concise manual
   checklist and log location, then inspect returned/local logs before claiming
   hardware acceptance. Keep story in Review until this evidence is accepted.

## Progress

- Remote beta verified; tracker ID audit: 206 files/rows, no duplicate or missing
  IDs, maximum root VP-0187. Assigned VP-0188.
- Assessment confirmed the save/load promotion, cross-backend fallback, missing
  unset option, warning mismatch, and duplicate renderer loads.
