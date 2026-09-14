# SDR transfer and LUT input gamma

The beta now uses pure gamma 2.4 as the unspecified SDR reference. No gamma
adjustment is the default; a source reference is an assumption, not detected
mastering metadata. BT.1886 is no longer offered in gamma selectors.

## Controls

| Setting | Default | Responsibility |
| --- | --- | --- |
| `sdr_input_transfer` | `2.4` | Assumed SDR reference for decoding; explicit power curves and sRGB are supported. |
| `sdr_adjust_gamma` | `passthrough` | Without a usable LUT, `on` converts that reference to the calibrated display response. Passthrough suppresses gamma conversion. |
| `output_gamma` | UI: `2.2`; omitted: accepted output transfer | Calibrated display response without a usable LUT. |
| `sdr_lut_input_gamma` | `passthrough` | With a usable LUT, optionally encode SDR into the declared LUT input gamma. Does not change the source decoding. |
| `hdr_tone_map_target_gamma` | `2.2` | HDR tone-map encoding into a usable LUT. Independent of all SDR gamma controls. |

In VP mode the combined Desired SDR gamma selector controls source reference
and adjustment. In LUT mode the SDR reference, SDR LUT input gamma and HDR LUT
input gamma are separate. The radio-button layout and dark styling are retained.
The LUT input gamma must be supplied by the operator; `.cube` files do not
reliably declare it. Gamut selection still chooses one of the three LUT slots.

Without a usable LUT, the SDR LUT input gamma is ignored and physical-display
settings apply. Changing a declared LUT input contract prevents a failed reload
from retaining a LUT validated for the previous contract. Range negotiation,
gamut mapping, scaling and dithering remain independent of gamma passthrough.

## Beta configuration loading

BT.1886 gamma selections resolve to pure 2.4. SDR Auto references also resolve
to 2.4. Auto/Off SDR adjustment resolves to passthrough. The renderer logs the
gamma substitutions; the editor translates the pending document, preserves
comments, adds a migration note, and saves only on Apply/OK with a backup.
Opening a file does not overwrite it. Explicit power/sRGB choices remain intact.

## Luminance scope

HDR target white and black remain in Rendering and do not control SDR luminance.
The fixed internal SDR reference is unchanged; configurable finite-black BT.1886
is outside this implementation. The white range now follows libplacebo: above 0.000001 through 10000 nits,
with live numeric validation. White must remain above effective black, including
after conversion to libplacebo floating-point values. Black must be finite, nonnegative and below white;
there is no arbitrary 1-nit cap. Saved HDR black zero still maps to 1e-6 nit.
Luminance diagnostics now print enough precision to distinguish this from unset.

These changes do not establish measured accuracy at the display. Bench tests
should cover SDR 2.4 to 2.2, passthrough, explicit SDR LUT encoding, HDR LUT
encoding, invalid/missing LUT fallback, and Full/Limited output separately.

## Verification

- Native policy, libplacebo luminance and configuration-core suites: 104 passed.
- Configuration UI suite: 72 passed; final default-label and layout fixes also
  passed the three focused calibration tests.
- Both calibration modes were captured and visually reviewed.
- x64 Release host, renderer and configuration builds completed.
- Display measurements and real Full/Limited HDMI captures remain outstanding.


## Limited transfer follow-up

The Output section reports disagreement between the selected profile's display
response and Limited transport setting. In VP calibration mode, a mismatch with
an explicit display gamma of 2.2 or 2.4 offers **Set Limited transfer to ...**.
This changes the selected profile's transport choice and derived G22 beta flag,
then uses normal Apply/OK persistence. It does not change calibrated pixels.
Other display gammas have no corresponding Limited transport choice; a LUT's
output gamma cannot be deduced from its input gamma, so neither case offers an
automatic correction. Opening the UI does not rewrite these settings.

Live status separately compares the last successfully rendered frame's target
transfer with the accepted DXGI declaration. Missing/failed frames report the
encoding as unavailable. An attached LUT reports its post-LUT gamma as unknown;
a missing LUT uses the actual fallback frame target. Full output does not show
the Limited-only diagnostic. Accepted transport does not imply gamma agreement.

Windows Studio G22 is not an exact pure-gamma-2.2 declaration (the BT.709 variant
includes a linear segment). Matching the numerical settings retains the approved
beta transport; it does not prove display-chain accuracy. G24 with a rendered
2.4 target is reported as agreement within VP, with the physical signal still
unverified. See [Microsoft's DXGI color-space definitions](https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type).

The previous 40–500-nit VP limit has been removed. Shared validation now accepts
finite white above 0.000001 through 10000 nits, with effective black below white.
The zero-black convention remains 1e-6 nit. Profile/rule runtime reads and the
editor use the same validation; invalid rules log the rejection and retain the
prior value. Invalid UI input remains visible and cannot overwrite saved values.
Black is no longer capped at 500; it must be nonnegative and below target white.
Inherited luminance is included in UI pair validation. Defaults, profile ownership,
SDR processing and styling are unchanged. See
[libplacebo 7.360.1 colorspace.c](https://github.com/haasn/libplacebo/blob/v7.360.1/src/colorspace.c).


Follow-up verification: x64 Release host, renderer and Config builds passed;
105 native policy/luminance/configuration tests and all 73 UI tests passed.
The UI regression covers profile inheritance, both correction directions,
derived flags, LUT and unsupported-gamma gating, and save/reload isolation.
Display-chain measurements remain outstanding. This follow-up was not deployed.


Expanded-luminance verification: the complete x64 Release solution build passed,
as did 106 native policy/luminance/configuration tests and all 74 UI tests.
Boundary coverage includes 0.000002, 0.01, 1, 39, 501, 600, 4000 and 10000 nits;
invalid, nonfinite, degenerate and float-rounded black/white pairs are rejected.
Libplacebo inference preserves the valid HDR targets, and SDR reference handling
remains unchanged across the expanded range. Existing user configuration was
not edited and these artifacts were not deployed.


## Cold-start versus renderer-switch diagnostics

Automatic `DISPLAY_STATE` log blocks are captured on the host before creating a
renderer, at renderer-ready, and before stopping it. This includes madVR and VP.
VP additionally captures before initializing its graphics device and before/after
DXGI colour-space negotiation. Each block identifies the PID, thread, sequence,
tick, phase, renderer, instance and target window, and records query duration.
There is no per-frame display-state polling and no display-setting write.

Readbacks include the target monitor/current mode, active Windows display paths
and fractional refresh, advanced-colour support/enabled state, reported encoding
and bit depth, Windows SDR-white level, GDI gamma-ramp fingerprints and samples,
DXGI output colour space/luminance/primaries, and swapchain exclusive-fullscreen
state. DXGI gamma readback is attempted only for an exclusive-fullscreen VP
swapchain. Host snapshots cannot access madVR's private swapchain. Every query
failure or missing monitor is reported as unavailable; it never means normal,
identity gamma or HDR off. Advanced colour enabled alone is not proof of HDR.
Legacy GDI gamma reports may not describe the advanced-colour scanout path.

Keep one SDR test pattern, settings, refresh rate and window mode unchanged:
cold-start VP, switch to madVR, return to VP, then close VP. Preserve the whole
session log before restarting (startup rotates logs). Repeat with a VP-only
renderer restart to distinguish a second-initialization effect from madVR-specific
state changes. Compare `DISPLAY_STATE` blocks with the existing source/formatter,
SDR_GAMMA, profile/action and output-contract logs. Output diagnostics can enable
additional existing pixel-contract details. A changed gamma fingerprint or OS
output state is evidence to investigate, not proof of the display's wire signal.

These APIs cannot verify HDMI quantization/InfoFrames at the receiver, internal TV
processing, or the physical image. `CheckColorSpaceSupport` reports capability,
not current colour-space readback. No guessed current swapchain colour-space
getter or automatic state reset is introduced. Readback calls may add transition
latency; block durations make that visible in timing-sensitive investigations.


Diagnostic verification: x64 Release host/renderer builds passed, and 107 native
tests passed, including actual desktop Windows/DXGI readback and unavailable
window handling. The desktop smoke snapshot took 16 ms on the development PC;
this is not a latency guarantee for other drivers. No picture-quality correction
is claimed by these logging changes, and no settings are automatically changed.
