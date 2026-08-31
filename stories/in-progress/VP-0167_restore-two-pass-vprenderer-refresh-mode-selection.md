# VP-0167: Restore two-pass VP Renderer refresh-mode selection

## Status

In Progress (reopened 2026-08-31). Live validation proved that the nominal-integer fallback at 9d3118f1 selected 24.000/60.000 for fractional 23.976/59.94 content.

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
## Correction in progress

2026-08-31: The nominal-integer EnumDisplaySettings path has been discarded. Commit 26877ce1 restores the exact rational SetDisplayConfig request and SDC_ALLOW_CHANGES, allowing Windows/the driver to resolve the compatible timing without collapsing 24000/1001 to 24 or 60000/1001 to 60. Release x64 renderer and GUI builds completed; 18 refresh-policy tests passed. Live verification of the corrected rational result is pending deployment.

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

## Deployment

2026-08-31: Rebase check against the current `origin/v1.3.004-beta` tip
`5d266d6a` was already current; VP-0167 remains commit `56d67d56`. The x64
Release `VideoProcessor-VPRenderer.vcxproj` and `VideoProcessor-GUI.vcxproj`
artifacts built successfully. The complete solution build was blocked only in
unrelated ConfigTests, OutputProbe, and Config projects by the host environment
having both `Path` and `PATH`, which MSBuild passes as duplicate CL.exe
variables. The modified VP Renderer project itself completed successfully.

Deployed only `vprenderer\VideoProcessorVPRenderer.dll`; no configuration file
was changed. The previous DLL is backed up at
`C:\Videoprocessor\vp\deployment-backups\vp0167-refresh-mode-selection-20260831-110656`.
The deployed DLL SHA-256 is
`B2E1288F132B6A88DF556EECD5331F9C8D65C1B7C73EEEA8D340B382BEF16859`, verified
against the Release build output. Live two-pass selection validation remains
required before moving this story to Review.


## Completion

2026-08-31: Live diagnostics confirmed that the two-pass policy selected the
60 Hz nominal fallback for 59.940060 Hz input from three enumerated display
modes. Windows rejected the original rational-only configuration request, so
the implementation was corrected to apply the concrete selected DEVMODE and
retry eligible timing variants on rejection. The final fixes were merged to
1.3.004-beta as 30a3a3b4, 2373f78, and 9d3118f1.

A clean x64 Release build was staged and verified against the 57-file release
manifest. The share package is
VP-0167-9d3118f-refresh-rate-fallback-tester.zip.
