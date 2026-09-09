# VP-0174: Color / Output configuration UI

Original implementation base: `v1.3.005-beta`, `38e7508f` (includes VP-0173).

The current contract is described below. Dated implementation/validation sections
record earlier stages and do not override the REQ-004 behavior.

## Current SDR/HDR calibration contract (REQ-004)

Each setting has one role. Color / Output contains **Display target**, **Target
gamut**, **Calibrated display gamma**, **Desired SDR gamma** (or **SDR reference for LUT** when LUT enablement is on), calibration
LUT enablement/files and **HDR tone-map target gamma**. These form one Color / Output
calibration profile. Rendering retains **Target nits**, **Target black**, quality
and general tone-mapping policy. Changing Rendering profiles cannot select a
different LUT or gamma declaration independently of Color / Output.

| Actual rendering state | Source description and target transfer |
| --- | --- |
| SDR with a usable attached LUT | Retain the declared source response and encode the pre-LUT target with that same response. The LUT performs reference-to-display calibration. |
| HDR with a usable attached LUT | Decode HDR normally; tone/gamut map, then encode with the Color / Output profile's explicit HDR target gamma, default 2.2, before calibration. |
| No usable attached LUT | Preserve the existing physical-display gamma path and saved SDR gamma-processing behavior. |

An enabled setting or populated filename does not establish that a LUT is usable.
A failed replacement can retain an existing usable LUT only under the same
contract. If none is attached, physical display gamma applies. A usable LUT makes
physical display gamma and the SDR gamma-processing switch inactive. Expected
SDR reference for LUT remains relevant for SDR with a LUT. HDR target gamma applies
only to HDR with a LUT; Target nits, Target black and dynamic tone mapping remain
active for HDR. The existing paired fixed SDR source/target minimum and maximum
luminance references are unchanged.

LUTs must be prepared for the expected SDR reference codes and selected target
gamut, and for the chosen HDR target encoding when processing HDR. This calibration workflow requires matching LUTs; arbitrary conversion or creative
LUTs can contain transforms that do not match it. Keeping source and
target transfers equal preserves the SDR transfer stage; gamut mapping and other
linear-light processing can still change pixels. It is not raw-byte passthrough.
The source description is retained for decoding, gamut conversion and scaling.

`target_primaries` replaces `sdr_target_primaries` and applies to SDR and HDR. It
selects the exact Rec.709, P3-D65 or BT.2020 calibration LUT slot. It describes a
reference/input gamut, not measured display gamut limits. No custom xy controls
are introduced. The old spelling remains accepted; canonical wins within a
section and a child alias overrides inherited baseline values.

`hdr_tone_map_target_gamma` is Color / Output-owned, accepts the explicit BT.1886,
sRGB and 1.8/2.0/2.2/2.4/2.6/2.8 responses, and defaults to 2.2. Old Rendering
`calibration_lut_input_transfer` values load as this HDR-only setting; `display`
becomes 2.2. Explicit canonical values win within the same section. The old
Color/base `calibration_lut_input_gamma` remains accepted but is ignored with a
diagnostic. Old Rendering calibration values migrate as described below. Within
Color, canonical alias reads do not rewrite the file; explicit edits rename an old
key in place, preserving comments, and an inheritance reset removes both spellings.
The explicit LUT path value `none` clears an inherited slot; omission inherits.

The UI applies live inactivity only when renderer evidence matches the current
configuration path/content identity and selected Color profile.
Physical display/conversion controls remain editable without confirmed attachment.
HDR tone-map target gamma follows the edited profile's LUT enablement: disabled
when unchecked, editable when checked even offline or during SDR playback. Help
distinguishes preparing a value from its actual live use. Saved enablement alone never marks calibration
active. Runtime status reports source/target transfer and actual LUT attachment;
physical HDMI/display response still requires measurement.

## Calibration ownership migration (2026-09-09)

The runtime and Config share one migration plan for old `vprenderer` profiles.
The literal Rendering root is the baseline when present; otherwise its first named
profile is the baseline. Existing Color profiles receive complete effective baseline
calibration values, preserving any already explicit Color calibration choices and
all original Color identities, rules and shortcuts.

Each other distinct effective Rendering calibration set becomes a manual variant
of every original Color profile, copying that Color profile's effective target,
gamma and output settings. Names identify both original profiles, with collision
suffixes where necessary. No old automatic rules or shortcuts are copied onto these
extra variants. Equivalent calibration sets are deduplicated. If explicit Color
settings replace the old Rendering baseline, that displaced set is also retained
as a manual variant. With no Color profiles, one default is created.

Complete migrated contracts contain enabled false, explicit `none` for every empty
slot and HDR target gamma 2.2 as defaults. This prevents accidental inheritance
from another set. Full original Rendering sections remain in inactive
`calibration_archive.*` sections with their settings and comments. Only calibration
keys leave the active Rendering sections. General processing settings, Target nits,
Target black and Rendering selection rules remain in place.

Runtime loading does this in memory; Config applies the same plan after earlier
split-profile migrations and backs up the original file on Save/Apply. Reloading a
migrated configuration is idempotent. Older non-target `[display]`/`[profiles.*]`
configurations retain their previous loading path rather than being reconstructed.

## Scope and compatibility

Color and Output use one profile family: `[vprenderer.color.<name>]`. One rule,
shortcut and active selection control both. Old Output page index 13 redirects
to the combined page at index 16. Settings retain their values, subject to the documented canonical aliases and REQ-004 transfer changes above.

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
new defaults; additional profiles inherit. Existing no-LUT gamma choices, including omitted or saved Auto values, retain their
interpretation. The active-LUT workflow follows the current REQ-004 contract above. The removal of independent Output selection is intentional. Removed Auto choices
appear as disabled legacy entries when necessary; changing to an explicit value
retires that selection. A missing LUT file is shown and preserved, not deleted.

| Control | Fresh profile | Auto treatment |
| --- | --- | --- |
| Target gamut | Rec.709 | Explicit |
| Display transfer | Gamma 2.2 | Removed; legacy requests preserved |
| Expected source SDR gamma | Gamma 2.2 | Removed; saved BT.1886 assumption retained |
| Enable SDR gamma processing | Checked (on) | Saved off/Auto retained until explicit edit |
| RGB range | Full | Removed; legacy requests preserved |
| Limited transport | 2.2 (inactive for Full) | Removed; legacy requests preserved |
| HDR tone-map target gamma | Gamma 2.2 | Explicit; HDR with a usable LUT only |
| Target nits | 100 | Numeric; HDR tone mapping only |
| Target black | 0 | Numeric; old Auto preserved |
| Dither target depth | 10 | Removed; old surface-following policy preserved |
| Dithering | Auto | Retained, including Off under Fast |
| Processing and scaling | Auto | Retained with resolved-value explanation |
| Presentation | Prefer flip (allow fallback) | Retained |

All explicit gamma choices stay together. Flip model replaces the misleading Direct
label; BitBlt model replaces Composed. Flip does not guarantee independent flip or
bypass desktop composition. Dither depth does not configure the GPU/HDMI wire depth.
Without a usable LUT, saved SDR handling off reinterprets source transfer using the accepted carrier.
It is displayed as a retained behavior, not an unchecked gamma checkbox.

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
| Rec.709, P3-D65 or BT.2020 / gamma 2.2 or 2.4 | Normal Windows full SDR declaration | Explicit 2.2 beta or 2.4, subject to acceptance | Target gamut; expected SDR response or HDR tone-map target gamma |

This is the source-level configuration contract, not certification of every driver,
projector or preview path. Embedded preview has constrained presentation/range.
Limited2.2 is intentionally included for beta feedback. Gamma/range/primaries remain
separate. With a usable LUT, SDR retains its declared source transfer at the input,
and HDR uses its explicit target gamma. Without one the physical display transfer is used. VP-0173 owns HDR destination
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

The UI uses **Desired SDR gamma** without configured LUTs and **SDR reference for
LUT** when LUT enablement is on. This label follows the edited profile, not live
attachment, so edits and offline use do not unexpectedly rename it. The underlying
`sdr_input_transfer` value and defaults are unchanged. With no usable LUT, the
same value supplies the desired response when SDR gamma processing is enabled;
this includes missing/rejected-LUT fallback. With a usable LUT, match the reference
used to create the LUT; the LUT determines the final displayed gamma.

BT.1886 is a black-dependent reference EOTF, not detected mastering metadata or a
synonym for pure gamma 2.4. VP's SDR source and target use fixed 203-nit white and
0.203-nit black (1000:1 reference contrast). UI help discloses this when BT.1886 is
selected; these are model assumptions, not measurements. HDR target BT.1886 uses
Rendering's Target nits and Target black instead, whether encoded directly for the
display or into a calibration LUT. The LUT must match that response. Pure 2.4 stays
an explicit choice; this UI change neither modifies pixel math nor substitutes a
new reference/default in saved profiles.

The madVR-style distinction is calibrated physical display response versus desired
SDR response. Envy's calibration workflow similarly requires matching the HDR LUT
gamma (2.2 is the example, 2.4 is supported when declared), and SDR's final gamma is
chosen in LUT generation. References: [Envy calibration guide](https://madvrenvy.com/wp-content/uploads/madVR-Envy-ColourSpace-Calibration-Guide.pdf?r=092),
[mpv SDR interpretation](https://mpv.io/manual/master/#options-sdr-adjust-gamma).

`output_gamma` describes physical display response. `sdr_input_transfer` describes
the expected SDR reference response, not a camera OETF. Without a usable LUT,
`sdr_adjust_gamma: on` converts a 2.4 reference to a 2.2 physical display as before.
Choosing 2.2 for both is deliberate, not a claim about every Rec.709 mastering.

With a usable calibration LUT, `sdr_input_transfer` establishes the declared SDR
source response and the pre-LUT target uses that same response. The gamma-processing
switch is inactive. The LUT must supply any reference-to-display correction.
For HDR, Color / Output's `hdr_tone_map_target_gamma` establishes the pre-LUT encoding;
HDR Target nits, Target black and dynamic tone mapping still apply.

Without a usable LUT, saved `passthrough`, `off` and `AUTO` retain their existing
SDR handling. With a LUT they do not replace the declared source transfer.
Disabled, missing or rejected LUTs with no retained usable fallback return to
physical display gamma. Range, gamut, scaling, shaders and dithering continue.

## Complete Color / Output setting inventory

| Existing key | Unified destination / treatment |
| --- | --- |
| target_primaries (old sdr_target_primaries alias) | Target gamut for SDR/HDR; exact LUT slot selection |
| output_gamma | Physical display transfer; all explicit/legacy values retained |
| report_bt2020_to_display | Display signaling request; retained |
| sdr_input_transfer | Desired SDR gamma without configured LUTs; SDR reference for LUT when enabled; one saved reference also used by no-LUT fallback |
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
| calibration_lut_input_gamma | Accepted but ignored; no compatibility UI |
| hdr_tone_map_target_gamma (old Rendering calibration_lut_input_transfer alias) | HDR-only pre-LUT transfer; default 2.2 |

Dithering, dither target depth and HDR nits/black remain Rendering-owned. LUT
enablement/files and HDR target gamma now belong to Color / Output through the
loss-preserving migration above.
The earlier conceptual mockup's Dithering section did not imply deleting or
silently moving per-Rendering-profile settings. Additional Output profiles are
archived in full, including unfamiliar keys; an unknown active baseline key is
not silently dropped and remains subject to ordinary validation.

## Direct UI descriptions and window order

SDR handling choices describe behavior: Convert SDR reference to target, Keep SDR
tone values unchanged, and Use transport transfer as SDR input. The saved on,
passthrough and off tokens and their behavior are unchanged. LUT input uses Same
as display transfer; saved automatic values and unset defaults are described
directly without calling them legacy. BitBlt is labeled by its presentation model.

Config responds to VP foreground activation by checking relative window order.
If VP's topmost fullscreen window has covered Config, Config raises itself without
requesting foreground. This is event-driven, preserves popup handling, and does not
add native cross-process ownership or recurring focus polling. The cross-process
regression checks actual relative order, not only the WS_EX_TOPMOST style bit.
This restores staying above VP; it does not disable VP's controls.

## Earlier SDR controls and Rendering-owned LUT input (2026-09-08)

At this earlier stage, the UI used Enable SDR gamma processing and Desired SDR gamma, separating
calibrated physical display response from desired viewing response. Checked saves
on; unchecked saves passthrough. Existing off and AUTO show a partially checked
checkbox with an explanation. Opening/saving preserves them; an explicit click
selects the new behavior. Desired gamma is inactive when processing is disabled.
All explicit gamma choices remain together. SDR input AUTO is no longer offered:
the current capture path maps generic SDR to BT.1886, not a detected mastering gamma.
Fresh desired/display gamma remain 2.2. P3-D65 labels and preset gamut support remain.

At this earlier stage, Rendering owned calibration_lut_input_transfer beside its LUT enablement and
files. One declaration applies to all three gamut slots. An explicit value,
including display, overrides the old calibration_lut_input_gamma after independent
profile selections are merged. Omission preserves the old Color/base contract;
no cross-product migration or silent rewriting is performed. The old Color setting remains loadable internally and has no separate UI control. Fresh Rendering profiles use explicit display. Display gamma
remains editable: no usable LUT means physical gamma, and an active LUT with input
display still depends on it. HDR Target nits, black and dynamic tone mapping remain
active, because these LUTs calibrate the tone-mapped result.

LUT reload identity includes file path, gamut, input transfer and constrained base.
For input display it also includes calibrated display gamma. Rejected same-contract
file reloads retain the previous valid LUT. A changed input declaration requires
revalidation; failure detaches the LUT and uses physical display gamma.

This follows the madVR distinction between calibrated display response and optional
gamma processing, without claiming identical implementation or copying HDR metadata
controls into SDR tone-mapping settings. Madshi's recommendation for dynamic tone
mapping plus an SDR calibration LUT: https://bugs.madshi.net/view.php?id=659.

### SDR/LUT follow-up validation

- Clean full x64 Release rebuild succeeded after switching to Qt's current
  checkStateChanged signal. No runtime changes followed that successful build.
- Native suite: 1,110/1,111 passed; the sole documentation inventory failure was
  corrected and its focused rerun passed. All renderer, LUT, profile ownership,
  compatibility, Full/Limited and 8/10-bit numerical checks passed.
- Updated full UI run: 63/66 passed. Three window/popup checks were intermittent
  in the suite; all three passed in separate focused processes. All changed SDR,
  inheritance, LUT persistence and migration scenarios passed in the full run.
- Six isolated actual-VP startup/render cases verified Rendering override,
  independent Color selection, omission fallback, Rendering inheritance, explicit
  display override and missing-LUT fallback. Valid identity LUTs attached with the
  expected input; missing LUT resolved to physical display gamma. Test-only
  configurations/state were used; active user configuration was untouched.
- Independent rendering/calibration specialist reviewed code and all six runtime
  logs and found no blocker for this focused beta change. Continuous live profile
  switching was inspected in code, not demonstrated by these separate launches.
  Physical HDMI/display response remains unmeasured.
- Release staging verified all 58 immutable files. Read-only Windows screenshot
  confirmed the combined profile page still loads against the active configuration.


## Earlier LUT control cleanup (2026-09-08)

At this earlier stage, Rendering > Display calibration LUT (3D LUT) contained
the single editable **Gamma expected by the LUT** control. Color / Output no longer displays a
duplicate saved-value row. Default and inheritance labels describe input gamma
without exposing the previous storage location. Existing configuration loading
and rendering behavior are unchanged.


## Scaling default cleanup (2026-09-08)

Automatic processing choices are labeled **Auto**, with the resolved setting
shown below the control. The default Scaling profile no longer offers an additional
**Use default** choice for Upscaler, Downscaler or Anti-ringing. Other Scaling
profiles offer **Inherit from default profile**, which remains distinct from Auto:
an inherited explicit scaler follows the default profile; Auto follows the active
quality preset. Configuration tokens, inheritance and renderer behavior are unchanged.
