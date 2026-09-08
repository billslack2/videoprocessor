# VP-0174: Color / Output configuration UI

Implementation base: `v1.3.005-beta`, `38e7508f` (includes VP-0173).

## Scope and compatibility

Color and Output share one scrollable tab. Their independently ordered profiles,
rules, shortcuts, configuration keys and runtime ownership remain separate. Old
Output page index 13 redirects to the combined page at index 16. Existing help
anchors and configuration tokens remain accepted.

This is a Config UI contract, not a wholesale change to renderer parser defaults.
A configuration created by the editor and the release sample explicitly store the
new defaults below. The first profile created in an empty family gets that family's
new defaults; additional profiles inherit. An existing configuration, including
omitted or saved Auto values, retains its effective behavior. Removed Auto choices
appear as disabled legacy entries when necessary; changing to an explicit value
retires that selection. A missing LUT file is shown and preserved, not deleted.

| Control | Fresh profile | Auto treatment |
| --- | --- | --- |
| Display primaries | Rec.709 | Explicit |
| Display transfer | Gamma 2.2 | Removed; legacy requests preserved |
| SDR input transfer | Gamma 2.2 | Renamed Follow source metadata |
| SDR handling | Honor input and display transfers | Removed; legacy requests preserved |
| RGB range | Full | Removed; legacy requests preserved |
| Limited transport | 2.2 (inactive for Full) | Removed; legacy requests preserved |
| Target nits | 100 | Numeric; HDR tone mapping only |
| Target black | 0 | Numeric; old Auto preserved |
| Dither target depth | 10 | Removed; old surface-following policy preserved |
| Dithering | Use quality preset | Retained, including Off under Fast |
| Processing and scaling | Use quality preset | Retained with resolved-value explanation |
| Presentation | Prefer flip (allow fallback) | Retained |

All explicit gamma choices stay together. Flip model replaces the misleading Direct
label; Legacy BitBlt replaces Composed. Flip does not guarantee independent flip or
bypass desktop composition. Dither depth does not configure the GPU/HDMI wire depth.
SDR handling Off remains labeled legacy: it reinterprets source transfer using the
accepted carrier, which can differ from calibrated display gamma.

## Derived Limited 2.2 flag

Editing range or Limited transfer saves `diagnostic_allow_limited_g22` true only
for Limited + explicit 2.2, and false for Full or Limited 2.4. The dormant transfer
is retained when switching to Full. Child-profile effective range/transfer is
included in synchronization. Diagnostic presets do not independently alter this
flag. Opening an older contradictory configuration preserves it and explains the
mismatch; editing the transport synchronizes it. The flag is read-only in the UI.

## Runtime evidence

The combined tab displays the running renderer's output status, separately from
pending edits: requested/effective transport, calibration target, interpreted SDR
transfer, presentation model, requested/effective dither depth, LUT status and
BT.2020 signaling request. A failed requested contract is prominently identified.
No running publisher means unavailable, never inferred success. This status does
not measure Windows' actual flip/composition promotion or physical HDMI signaling.
The shared snapshot uses a new versioned mapping to avoid reading older layouts.

## Calibration combinations

| Display target | Full carrier | Limited carrier | LUT coordinates |
| --- | --- | --- | --- |
| Rec.709, P3-D65 or BT.2020 / gamma 2.2 or 2.4 | Normal Windows full SDR declaration | Explicit 2.2 beta or 2.4, subject to acceptance | Configured target primaries and display transfer |

This is the source-level configuration contract, not certification of every driver,
projector or preview path. Embedded preview has constrained presentation/range.
Limited2.2 is intentionally included for beta feedback. Gamma/range/primaries remain
separate; LUT enablement never selects a different gamma. VP-0173 owns HDR destination
white/black handling; these controls do not alter SDR brightness.

## Validation

See the final validation results recorded in the story tracker. Focused UI tests
cover fresh defaults, legacy preservation, derived flag save/reload, independent
profile inheritance and missing LUT preservation. Release solution/native tests
and visual review are required before the implementation is marked ready.

### Implementation verification (2026-09-08)

- Full x64 Release solution build succeeded.
- Native suite: 1,102 passed, including VP-0173 GPU readback fixtures.
- Config suite: 65 scenarios exercised; full run passed 64, with the remaining
  legacy-label assertion corrected and passing on final focused rerun. Fresh-default
  and legacy/inheritance tests were also rerun successfully after review fixes.
- Independent review findings fixed: synchronize only affected profiles, prevent
  root omission from recreating retired Auto, and handle explicit reactivation.
- Windows screenshot reviewed. Profile name, shortcuts and rule remain always
  visible, matching other profile tabs. Offscreen rendering lacks fonts on this host and was not used for QA.
- Release packaging verified 58 immutable files. No active configuration migration
  or physical HDMI/display calibration measurement is part of this deployment.
