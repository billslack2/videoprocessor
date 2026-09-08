# VP-0174: Color / Output configuration UI

Implementation base: `v1.3.005-beta`, `38e7508f` (includes VP-0173).

## Scope and compatibility

Color and Output use one profile family: `[vprenderer.color.<name>]`. One rule,
shortcut and active selection control both. Old Output page index 13 redirects
to the combined page at index 16. Settings within the profile retain their keys.

Legacy loading copies every non-selector setting from the first Output baseline
into each Color profile, without overriding already unified explicit values.
A literal Output root is the old inherited baseline and takes precedence over
named variants. Without any Color profiles, one default Color profile is created.
All original Output sections become inactive `legacy_output` records; their
settings, unknown keys, shortcuts, rules and comments remain in the file. Extra
Output selections no longer run. Existing Color names, rules and shortcuts survive.
The runtime applies the same migration in memory before profile selection. Config
saves it only on Apply/Save, backing up the exact original file before replacement.
Reopening a migrated file does not reapply or duplicate the migration.

This is a Config UI contract, not a wholesale change to renderer parser defaults.
A configuration created by the editor and the release sample explicitly store the
new defaults below. The first profile created in an empty family gets that family's
new defaults; additional profiles inherit. Existing gamma choices, including omitted or saved Auto values, retain their
interpretation. The removal of independent Output selection is intentional. Removed Auto choices
appear as disabled legacy entries when necessary; changing to an explicit value
retires that selection. A missing LUT file is shown and preserved, not deleted.

| Control | Fresh profile | Auto treatment |
| --- | --- | --- |
| Display primaries | Rec.709 | Explicit |
| Display transfer | Gamma 2.2 | Removed; legacy requests preserved |
| SDR reference / intended response | Gamma 2.2 | Renamed Follow source metadata |
| SDR handling | Preserve reference appearance | Removed; legacy requests preserved |
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
| Rec.709, P3-D65 or BT.2020 / gamma 2.2 or 2.4 | Normal Windows full SDR declaration | Explicit 2.2 beta or 2.4, subject to acceptance | Configured target primaries and declared LUT input transfer |

This is the source-level configuration contract, not certification of every driver,
projector or preview path. Embedded preview has constrained presentation/range.
Limited2.2 is intentionally included for beta feedback. Gamma/range/primaries remain
separate. With a usable LUT, the explicit LUT input transfer selects pre-LUT
encoding; without one the physical display transfer is used. VP-0173 owns HDR destination
white/black handling; these controls do not alter SDR brightness.

## Validation

See the final validation results recorded in the story tracker. Focused UI tests
cover fresh defaults, legacy preservation, derived flag save/reload, unified
profile inheritance and missing LUT preservation. Release solution/native tests
and visual review are required before the implementation is marked ready.

### Unified-profile implementation verification (2026-09-08)

- Clean full x64 Release solution rebuild succeeded. Incremental builds encountered
  corrupt Visual Studio PDBs; only the successful clean-build runtime is deployed.
- Native suite: 1,107 cases exercised; 1,106 passed in the full run. Its sole
  documentation-inventory failure was fixed and passed on focused rerun.
- Config UI: final complete run passed all 66 scenarios, exit 0.
- Numerical WARP/libplacebo readbacks at 8/10-bit Full/Limited verify 2.2 unity,
  2.4-reference-to-2.2 conversion, and equivalent conversion through a LUT once.
  Deliberately applying conversion twice produces distinguishable failure values.
- Migration tests cover every original Color/Output key, rules/shortcuts,
  inactive unknown settings/comments, archive-name collisions, malformed sections,
  original-byte backup, runtime ownership and idempotent reload/save.
- Independent specialist source review found no migration or double-gamma blocker.
- Windows screenshot verified one profile list and always-visible name/shortcuts/rule.
- A physical second monitor was unavailable; synthetic negative-origin placement
  and live target-window checks ran. HDMI/display response remains unmeasured.

## SDR reference and LUT domain

`output_gamma` describes the physical display response. `sdr_input_transfer`
describes the SDR reference response to preserve, not the camera OETF. With
`sdr_adjust_gamma: on`, a 2.4 reference and 2.2 display receive a 2.4-to-2.2
conversion. The preserved fresh choice 2.2-to-2.2 is a deliberate 2.2 response,
not a claim that every Rec.709 source was mastered for it. BT.1886 and sRGB remain
available alongside every existing pure-power value.

`calibration_lut_input_gamma` is the expected transfer at an active LUT's input.
`display` (also the omission default) preserves the previous output_gamma behavior.
An explicitly selected BT.1886/2.4 LUT domain permits a full reference-to-display
LUT to own that correction exactly once. Missing, rejected-without-active-fallback,
or disabled LUTs use physical display gamma. LUT enablement and paths remain in
Rendering; target nits/black remain HDR-only there as well.

`sdr_adjust_gamma: passthrough` is a new explicit choice: reinterpret SDR in the
resolved pre-LUT/display domain, preserving its tone response through that transfer
stage. Range, gamut, scaling, LUT and other processing still apply. `off` retains
its old carrier-based interpretation for saved compatibility. HDR input ignores
SDR reinterpretation and still tone maps into the selected output/LUT domain.

## Complete Color / Output setting inventory

| Existing key | Unified destination / treatment |
| --- | --- |
| sdr_target_primaries | Display calibration; retained |
| output_gamma | Physical display transfer; all explicit/legacy values retained |
| report_bt2020_to_display | Display signaling request; retained |
| sdr_input_transfer | SDR reference / intended response; retained |
| sdr_adjust_gamma | SDR handling; legacy values retained, explicit passthrough added |
| output_presentation | Output transport; retained |
| output_range | Output transport; retained |
| output_transport_gamma | Limited transport; retained |
| output_path_profile | Diagnostic preset; retained |
| output_diagnostics | Diagnostics; retained |
| diagnostic_disable_shader_cache | Diagnostics; retained |
| diagnostic_disable_compute | Diagnostics; retained |
| diagnostic_force_8bit_sdr_swapchain | Diagnostics; retained |
| diagnostic_allow_limited_g22 | Derived status; saved legacy values retained until transport edit |
| diagnostic_allow_full_g22 | Compatibility key retained; still not a new UI choice |
| diagnostic_vp_owned_dxgi_presenter | Diagnostics; retained |
| Color name, shortcut, cycle_shortcut, when | One common profile identity and activation |
| Old Output identity, shortcut, cycle_shortcut, when | Preserved in inactive legacy_output sections |
| calibration_lut_input_gamma (new) | Explicit LUT input-domain selector |

Dithering, dither target depth, HDR nits/black, and LUT enable/path controls were
Rendering-owned before this merge and remain there, with all values preserved.
The earlier conceptual mockup's Dithering section did not imply deleting or
silently moving per-Rendering-profile settings. Additional Output profiles are
archived in full, including unfamiliar keys; an unknown active baseline key is
not silently dropped and remains subject to ordinary validation.
