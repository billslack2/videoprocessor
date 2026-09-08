# VP-0172: Source-driven output display-mode selection

## Status

Backlog (2026-09-07). Derived from the operator's documented output-link
failure and true-4K presentation requirements. This is a deliberate extension
of VP-0167: VP-0167 selects a refresh rate at the current desktop raster;
VP-0172 selects an operator-permitted complete display mode (raster and rate).

## User story

As a VP Renderer operator, I want to supply an ordered list of permitted
output display modes and have VP select one for each source cadence, so I can
keep unsupported raster/rate combinations off the wire and explicitly choose
1:1 DCI output when my display path supports it.

## Product contract

The operator, not VP, owns raster policy. Configuration supplies one ordered
list of complete permitted output modes. VP uses its existing two-pass
rational-rate policy to determine eligibility and chooses the earliest listed
eligible mode. It must not infer a mode from EDID, link bandwidth, observed
failures, or source raster.

Long form `WIDTHxHEIGHTpRATE` is canonical and always accepted. The existing
short form `HEIGHTpRATE` resolves to the documented 16:9 width for that
height; it never depends on source raster or enumerated modes. Consequently,
`2160p24` means `3840x2160p24`, never 4096x2160. DCI modes have no short form,
and all diagnostics print long form. The stored rate remains an exact rational
(`23` = 24000/1001, `59` = 60000/1001, and so on); interlaced entries are
rejected.

With no configured list, behavior remains VP-0167 behavior exactly: refresh
may change, desktop raster does not.

## Scope

1. Add a renderer/output configuration field, editor control, validation, help,
   and diagnostics for an ordered list such as
   `3840x2160p23 4096x2160p24 1920x1080p59`.
2. Extract a platform-independent, unit-testable display-mode value and parser;
   compose it with VP-0167's exact/tight/bounded-fallback rational ranking
   rather than duplicate that policy.
3. Enumerate progressive 32-bpp modes from both DXGI and GDI, union and dedupe
   by `(width, height, rational rate)`, and log whether each mode came from
   DXGI, GDI, or both. Do not discard modes merely because their raster differs
   from the current desktop.
4. Carry the selected width, height, and rational rate through the existing
   display transition. Request all three in one `SetDisplayConfig` operation;
   preserve the current virtual-mode versus non-virtual-mode index handling.
5. Verify the applied width, height, and refresh after the transition. Candidate
   rollback and retirement restore must restore the entire pre-VP display mode,
   not only the rate.
6. Emit source raster/cadence, enumerated and eligible modes, selected long-form
   mode, selection path, and named skip/failure reasons. If a configured mode
   is not enumerated, print it and the enumerated set. Once per source change,
   explain when a DCI source was not selected because no full `4096x2160` entry
   was configured.

## Non-goals

- HDR/color transport, monitor topology, target-only display-session policy,
  scaler policy, viewport/aspect policy, or per-raster calibration.
- Creating or requesting custom display timings, EDID/link-bandwidth inference,
  or automatic source-raster matching.
- Routing mode selection through legacy refresh commands or event actions.

## Acceptance criteria

- Parser tests prove canonical long forms, fixed 16:9 short-form resolution,
  malformed/interlaced rejection, and distinct UHD/DCI modes at the same rate.
  `2160p24` resolves to 3840x2160 even when the source and enumerated modes are
  DCI.
- Rate matching preserves VP-0167 exact, tight, and bounded-fallback behavior;
  at one cadence, the earliest configured eligible raster wins.
- With `3840x2160p23 3840x2160p24 1920x1080p59 1920x1080p60`, 23.976 and
  59.94 sources select 3840x2160p23 and 1920x1080p59 respectively, the wire
  raster matches, one display transition occurs per source change, and the
  pre-VP full mode is restored on retirement.
- With an explicit DCI-first list and an enumerated 4096x2160p24 mode, a DCI
  source selects that mode and is presented 1:1 without horizontal resampling
  or 16:9 pillarboxing. A 16:9 source at the same cadence follows list order
  and retains aspect-preserved fitting.
- A listed but unavailable DCI mode produces the named diagnostic and playback
  continues at the next eligible listed mode.
- With no list, existing refresh-only behavior is unchanged. Embedded-child
  presentation still skips display-global mode changes.
- Focused tests and x64 Release builds pass; bench validation verifies the
  applied wire raster, one transition per source change, and restore behavior.

## Dependencies and readiness

- VP-0167 owns the existing rational candidate ranking, verification, and
  rollback behavior. Its tracker state is Review; confirm its acceptance
  coverage before extending the existing transition policy.
- Before implementation, verify the current configuration namespace and exact
  `SetDisplayConfig` mode-info ownership on the current beta integration base.
- The DCI bench case requires the target driver to enumerate the requested DCI
  timing; VP must not create it.
