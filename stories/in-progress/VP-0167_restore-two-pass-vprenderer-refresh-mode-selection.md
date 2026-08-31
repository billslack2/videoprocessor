# VP-0167: Restore two-pass VP Renderer refresh-mode selection

## Status

In Progress (2026-08-31). The user confirmed `v1.3.004-beta` as the
implementation base at `5d266d6a`. Work is on
`codex/vp-0167-refresh-mode-selection` in
`C:\Videoprocessor\vp\vprenderer\.codex-worktrees\vp-0167-refresh-mode-selection`.

VP Renderer currently submits one content-derived rational refresh request to
`SetDisplayConfig` with `SDC_ALLOW_CHANGES`; it does not enumerate and rank
the target display's supported modes. This leaves the choice of an unsupported
exact rate to Windows and does not implement the established madVR-style
fallback policy.

The creation audit found 164 canonical story files and 186 index rows. The
pre-existing count mismatch is recorded here and is not silently repaired.

## Implementation progress

2026-08-31: Added a platform-independent two-pass ranking helper and unit
tests: exact rational equivalence or tight driver rounding wins first; only a
0.5% closest-in-range candidate can be used as fallback, with a deterministic
tie-breaker. VP Renderer now enumerates candidate target refresh rationals for
the active source path, logs its selected path, and retains the current mode
when enumeration finds no acceptable candidate. The x64 Release VP Renderer
and policy-test projects build successfully; the four new ranking tests pass.
## User story

As a VP Renderer user, I want automatic refresh switching to choose the best
supported display mode: first an exact or tightly equivalent cadence match,
then the closest safe in-range compatible mode when no exact match exists, so
playback uses a predictable cadence rather than an opaque Windows fallback.

## Scope

1. Enumerate the connected target display's modes using the Windows display
   configuration APIs without changing desktop resolution or topology.
2. Rank candidates in two explicit passes: exact/tightly equivalent cadence
   first, then a bounded closest in-range fallback.
3. Preserve the existing 2x policy for interlaced and approximately 25/30 Hz
   input, and preserve refresh restoration ownership.
4. Submit the selected supported rational mode, not merely the content-derived
   requested rational, through the existing display-transition path.
5. Emit compact diagnostics containing requested cadence, eligible exact and
   fallback candidates, selected mode, selection path, and failure reason.
6. Extract platform-independent candidate ranking into unit-testable code.

## Non-goals

- Do not alter desktop resolution, HDR/color transport, monitor topology, or
  target-only session policy.
- Do not loosen restore verification or treat a successful API call as a
  confirmed physical display transition.
- Do not use legacy refresh-command or action routing as part of mode
  selection.

## Acceptance criteria

- A 23.976 source selects a supported 24000/1001 mode in preference to a
  nearby integer mode.
- Equivalent rational representations compare as the same exact candidate.
- When no exact candidate exists, a supported candidate in the documented
  fallback window is selected deterministically by distance and tie-breaker.
- An out-of-range candidate is never selected merely because it is closest.
- A source with no acceptable candidate leaves the current display mode intact
  and logs the reason.
- Existing 25/30/interlaced 2x selection and refresh restoration remain
  correct.
- Unit tests cover exact preference, fallback ranking, tie-breaking, boundary
  rejection, and rational-equivalence cases; the x64 Release build passes.

## Dependencies and readiness

The implementation must begin from a clean worktree based on the confirmed
current beta integration branch. The discovered default branch on 2026-08-31
is `v1.3.004-beta`; confirmation is required before source implementation.

## Validation

2026-08-31: Source commit `56d67d56` (`Restore two-pass VP Renderer refresh
selection`) completed on `codex/vp-0167-refresh-mode-selection`.

- `Release|x64` build passed for `VideoProcessor-VPRenderer.vcxproj`.
- `Release|x64` build passed for `VideoProcessor-Test.vcxproj.vcxproj`.
- Targeted VSTest policy selection passed: 4/4 new exact, fallback, rejection,
  and tie-breaker cases.

Live validation remains required before deployment: test a display with an
exact 24000/1001 mode, one requiring the bounded 24.000 fallback, and one
with no same-family fallback. Verify the `libplacebo refresh-rate selection`
log reports the expected path and the desktop resolution remains unchanged.
