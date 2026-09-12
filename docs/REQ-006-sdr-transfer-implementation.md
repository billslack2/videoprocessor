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
is outside this implementation. Existing white bounds remain 40–500 nits, with
live numeric validation added. Black must be finite, nonnegative and below white;
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
