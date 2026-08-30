# VP-0166: Correct integrated target-frame 3D LUT application

## Status

In Progress. Created on 2026-08-29 as the consolidated replacement for VP-0100
and VP-0101; the consolidation was committed and synchronized to tracker
`main` as `c0a5480641c20101bb34392315687fbc20b68774` before source work began.

Implementation started on 2026-08-29 from freshly fetched
`origin/v1.3.004-beta` at
`66f22307158b2da40f2cdb0ee398ea6ab62c5997`, on source branch
`codex/vp-0166-lut-contract` in the clean worktree
`C:\Videoprocessor\vp\vprenderer\.codex-worktrees\vp-0166-lut-contract`.
The first slice resolves a typed Rec.709/SDR-BT.2020 LUT-input and carrier
contract, semantic-zero black handling, and focused contract tests before the
reload-state refactor.

Readiness review is complete: the target-frame libplacebo attachment,
presenter, and backbuffer are suitable; the remaining work is a bounded LUT
contract and lifecycle correction, not a renderer or presenter redesign.

## User story

As a calibrated-display user, I want VP Renderer to feed a 3D `.cube` file the
exact full-range Rec.709 or SDR BT.2020 encoded RGB reference space for which it
was authored, apply it once at the final video-picture calibration stage, and
preserve predictable dither, reload, fallback, and diagnostics, so the
displayed video is calibrated without an accidental second color transform or
a silently mismatched cube.

## Decision and implementation boundary

VP-0011 and VP-0012 selected the correct architectural seam:

`source decode -> scale -> tone/gamut map -> target primaries/transfer ->`
`full-range target encode -> target PL_LUT_NATIVE -> final video dither`

Keep that seam. It matches mpv/libplacebo's target/display-LUT model and the
broad ordering used by MPC-BE's internal ICC-LUT path. External MPC Video
Renderer does not implement user 3D LUTs and is not a contract model.

It also matches madVR's important color-contract behavior: the gamut named by
the calibration LUT is the RGB input gamut madVR prepares before lookup. A
BT.2020 source with a BT.2020-input LUT is not first converted to Rec.709; a
Rec.709 source is converted to the declared BT.2020 reference when that is what
the cube expects. PC madVR's binary `.3dlut` and VP's `.cube` are not file- or
range-compatible, however: PC madVR uses video-level RGB at its LUT boundary,
while VP-0166 deliberately defines full-range normalized Cube input.

This first production contract is explicitly a **video-picture calibration
LUT**, not a creative LUT, source-normalization LUT, color-conversion
replacement, ICC workflow, or whole-backbuffer shader. The active video
picture passes through the cube. VP's current target overlays (statistics,
sweep, and profile notices), diagnostic overlays, and out-of-picture
clears/borders are composited or cleared later and do not. Subtitle pixels
already baked into the video picture do pass through it; this story does not
invent or make claims for a separate subtitle-overlay path that VP does not
currently construct.

No presenter redesign is in scope. Whole-backbuffer calibration would require
a separately approved final composed-frame LUT-and-dither pass. During
calibration measurement VP must suppress optional OSD/diagnostic overlays, and
an accepted v1 cube must preserve a neutral zero black so bypassed zero-black
borders do not acquire a visible chromatic or level discontinuity.

## Current implementation assessment

The `004-beta` code already has useful bounded loading, path containment,
3D-only dimension limits, cached libplacebo resources, target-native
attachment, diagnostics, and no-LUT fallback. The following defects or missing
contracts must be corrected together:

1. Target/signal equality is calculated before the renderer replaces target
   fields with the selected calibration target. The stale boolean is then used
   to accept or reject the LUT. Equality of reference target metadata and the
   presentation carrier is not the same as proof that the carrier preserves
   the produced codes.
2. `AUTO` reference fields are wildcards. A cube can remain accepted after a
   renderer/profile target changes. Every field must instead resolve to a
   concrete value before activation.
3. LUT reference white exists but reference black does not. This makes a true
   BT.1886 authoring contract unverifiable.
4. The public configuration can request P3-D65 even though the runtime rejects
   it. Merely accepting a P3 enum would still be unsafe: DXGI has no ordinary
   SDR RGB P3 swapchain declaration, and a Windows-managed transport would not
   preserve opaque device-native cube output as semantic P3.
5. At `PL_LUT_NATIVE`, a limited 10-bit target presents nominal black and white
   to the cube at `64/1023` and `940/1023`. BT.1886 picture values instead use
   `(D-64)/876`. Treating these domains as interchangeable is incorrect.
6. Pinned libplacebo parses non-default `DOMAIN_MIN/MAX` by rescaling table
   outputs rather than retaining Adobe's input-domain transform. Such cubes
   can load but mean the wrong thing.
7. Parsed, contract-active, and runtime-rejected states are conflated. This is
   a latent dither-state bug: pinned presets do not currently select error
   diffusion, but an incompatible path could remain disabled after a cube is
   rejected or fails at render time.
8. Replacing a cube's contents at the same path is not a reload key. The old
   resource can remain resident until the path or renderer changes.
9. The current tests prove basic parsing and a gross identity/non-identity GPU
   difference, but do not independently prove cube ordering, tetrahedral
   interpolation, target contract, clamp, profile invalidation, same-path
   reload, or failure-state dither restoration.

## Resolved v1 calibration contract

### LUT role, target, and carrier

- Attach exactly one display-calibration cube to the libplacebo target frame
  with `PL_LUT_NATIVE`.
- The cube receives encoded target RGB after source interpretation, scaling,
  HDR-to-SDR tone mapping where required, gamut mapping, target primaries,
  target transfer, and target representation encoding.
- The cube's outputs are device/native drive codes. Do not relabel those
  outputs with the cube's input primaries or call them a generic P3/BT.2020
  wire signal.
- Keep ordinary final video dithering after the cube. Disable error diffusion
  only while an active LUT makes it incompatible; restore the selected normal
  dither path immediately when the LUT is inactive or rejected.
- Replace `targetMatchesSignaledOutput` as the activation concept with two
  independent facts: the resolved LUT-input target matches the cube contract,
  and exact carrier evidence matches the supported v1 predicate below. A
  carrier's DXGI label need not duplicate the calibration target label, but VP
  must not claim that the label describes post-LUT native codes.
- V1 supports only a verified legacy SDR path with Windows Advanced Color/HDR
  off and no competing ICC/MHC/VCGT/device correction. A physical
  display-correction cube and the OS, GPU, or display must not both own the same
  correction. Disabling libplacebo ICC alone is insufficient: Windows can
  automatically color-manage integer swapchains while Advanced Color is on,
  and per-process legacy-ICC compatibility is not fully controlled by VP. The
  profile and diagnostics must state the required OS/display mode. The bounded
  automatic guard queries Advanced Color/HDR and rejects enabled or unknown
  state; VP does not claim it can discover projector-internal modes, vendor GPU
  ramps, or every external correction. Activation additionally requires an
  explicit profile attestation that those unobservable competing corrections
  are disabled: `lut_external_color_management: none_attested`. Missing,
  `AUTO`, or any other value fails closed.

The common v1 carrier predicate is:

- `actualOutput.safeToRender` and `requestedEncodingActive` are true;
- encoding is `FULL_G22_P709`, used only as the inert Windows/DXGI carrier;
- presentation is the existing top-level VP-owned R10 `FLIP` path with direct
  delivery separately attested through
  `lut_direct_delivery_authority: external_attested`; a flip swap effect alone
  is not evidence of DirectFlip/MPO/independent-flip delivery. Child preview,
  composed/`BITBLT`, and an unattested flip candidate reject the LUT;
- negotiated format is `DXGI_FORMAT_R10G10B10A2_UNORM`, effective sample and
  color depth are 10 bits with no bit shift, and `display_bit_depth` resolves
  to 10;
- target primaries are the resolved Rec.709 or BT.2020 LUT-input gamut and the
  target representation is full RGB;
- `actualOutput.targetTransfer` and the final target-frame transfer equal the
  resolved, supported LUT-reference transfer;
- Windows Advanced Color/HDR is confirmed off, no libplacebo target ICC is
  attached, and the profile contains the external-correction attestation; and
- monitor/output identity and carrier generation match the evidence recorded
  when the contract was resolved.

Any other presentation model, encoding, format/depth, color-management state,
or stale generation rejects the LUT while leaving ordinary playback available.

Target-specific display-mode authority is also mandatory:

- A Rec.709 cube requires a non-empty named calibrated display mode and
  `lut_display_mode_authority: manual_attested`.
- A BT.2020 cube requires `targetBt2020`, a final target-frame primaries value
  of `PL_COLOR_PRIM_BT_2020`, and one of two authorities:
  `manual_attested`, meaning the named projector BT.2020/native mode and RGB
  Full link were deliberately selected and externally verified; or
  `nvidia_external_verified`, meaning NVAPI SET/readback is combined with
  stored external analyzer evidence for RGB + BT.2020 + Full, 10 bpc, code
  preservation, delivery mode, and projector mode.
- The current `NvidiaBt2020Reporter::IsReadbackVerified()` proves only AVI
  colorimetry SET/readback. It does not prove physical-link RGB/range/depth,
  code preservation, direct delivery, or projector mode and must not map by
  itself to calibration authority. If signaling is required and NVAPI fails,
  VP rejects the LUT instead of continuing with signaling silently unavailable.

These gates harden the already implemented `004-beta` target/P709-carrier/R10
presenter path; they do not introduce another presenter seam. Promote that
existing path behind a production selector,
`output_presentation: calibrated_direct`, which selects the top-level VP-owned
R10 path and its guards. A production LUT profile must not require users to
enable the diagnostic-only `diagnostic_vp_owned_dxgi_presenter` switch.

The selected profile binds the cube hash and contract to an EDID hash,
DisplayConfig adapter LUID and target ID, output technology/connector path,
active width/height, refresh numerator/denominator, scanline/scaling state,
adapter LUID and driver version, named display mode, TRC, `Lb/Lw`, direct-link
attestation, and signaling authority. Unknown identity/evidence rejects the
LUT. Display/topology change, monitor crossing, hotplug, mode/refresh change,
device reset, swapchain generation, driver identity, or profile change advances
the calibration contract generation and re-resolves it. Public configuration
includes `lut_display_mode: <non-empty label>` alongside the authority,
direct-delivery, and external-color-management attestations.

### One atomic resolved contract

Resolve one immutable `calibrationContractGeneration` before attaching a cube.
It contains at least:

- cube canonical path, content signature/fingerprint, dimensions, and reload
  generation;
- role (`target_display_calibration`) and scope (`video_picture`);
- input primaries (`REC709` or `BT2020` for v1);
- input transfer (`sRGB`, pure `GAMMA22`, pure `GAMMA24`, or `BT1886` with
  Rec.709; pure `GAMMA22` or `BT1886` with BT.2020; other parsed target
  transfers remain unavailable until added to the tested matrix);
- input range (`FULL` for v1);
- reference white and black luminance in nits;
- resolved tone- and gamut-mapping policy;
- output/native profile label, display-mode authority, and installation
  requirements;
- presentation carrier identity/capability generation;
- presentation model/owner, DXGI encoding, swapchain format, sample/color
  depth, bit shift, effective display depth, monitor/output identity, detected
  Advanced Color state, and external-correction attestation; and
- activation state (`Disabled`, `Parsed`, `ContractRejected`, `Active`, or
  `RuntimeRejected`) plus a concise reason; and
- a separate optional reload-candidate identity, signature, status, and reason,
  so a rejected same-contract candidate does not replace the identity of the
  last-known-good active resource.

All existing profile inheritance and rule selection must finish before this
object is created. Changes to any field invalidate the object atomically.

`AUTO` never means wildcard. For each LUT-reference field it means “inherit the
final concrete value from the selected renderer target profile.” If no concrete
value exists, disable the cube with an actionable reason. The `.cube` file
cannot supply primaries, transfer, range, white, black, or role. For BT.1886,
both authoring white and black must ultimately come from explicit profile
values; an auto-derived black such as `white/1000` is not sufficient.
Pure `GAMMA22` and `GAMMA24` profiles likewise require explicit authoring
`Lw/Lb`; an inherited or auto-derived black cannot verify the declared
black-scaled power curve against the cube-authoring contract.

BT.2020 is a first-class SDR LUT-input gamut in v1. It does not imply PQ, HLG,
HDR passthrough, display coverage, or a transfer function. Rec.709 SDR and HDR
BT.2020/PQ/HLG sources are decoded and tone/gamut mapped as needed into the
declared SDR BT.2020 target, then encoded with the profile's explicit SDR
transfer before the cube. A cube authored to receive PQ or HLG, or to perform
external HDR tone mapping, is a different stage and is rejected.

The two accepted BT.2020 profile shapes are
`BT2020_D65_BT1886_FULL_R10` with explicit `Lb/Lw`, and
`BT2020_D65_G22_FULL_R10` only for a cube explicitly authored for pure gamma
2.2 with explicit `Lb/Lw`. They use BT.2020 primaries R `(0.708,0.292)`, G `(0.170,0.797)`, B
`(0.131,0.046)`, and D65 `(0.3127,0.3290)`. For the G22 profile the luminance
model is `L = Lb + (Lw - Lb) * V^2.2`; do not call gamma 2.2 a “BT.2020
transfer.”

P3-D65 remains deferred. It names only RGB chromaticities and D65 white; it
never implies sRGB, gamma 2.2, 2.4, 2.6, or another transfer. A later P3
extension must select and prove either a pixel-transparent device-native
carrier or a declared BT.2020/scRGB/Windows-managed transport whose cube output
contract is designed for that compositor. It cannot activate merely because a
target enum parses.

### Full-range-only production boundary

V1 accepts full-range Rec.709 or SDR BT.2020 encoded input coordinates:
normalized cube coordinate 0 is target code 0 and coordinate 1 is the maximum
target code. Its table outputs are normalized opaque native-drive values in
`[0,1]`; calling them “full-range Rec.709/BT.2020 output” would incorrectly
assign reference-space semantics after the device cube. Reject a LUT profile
when the resolved target input representation or transport is limited/studio,
while continuing safe ordinary playback without the cube.

Tetrahedral sample coordinates intentionally clamp to `[0,1]` in v1. Negative,
extended, and super-white target values therefore use edge texels rather than
extrapolation. This behavior must be explicit in diagnostics/documentation and
covered at `-epsilon`, `0`, `1`, and `1+epsilon`; v1 does not claim to preserve
an extended-range calibration domain through the cube.

Limited-range support is a future bounded extension. It must either normalize
nominal picture codes before the cube and restore storage codes after it, or
define and validate an explicitly named storage-code-authored cube contract.
It must not call a `64/1023...940/1023` storage-domain cube a generic BT.1886
`0...1` cube.

For a true BT.1886 profile, use the standard black-aware function over
normalized picture value `V` with explicit authoring `Lw` and `Lb`. When
`Lb=0`, it reduces to pure gamma 2.4. Diagnostics and documentation must not
use “BT.1886” as shorthand for every gamma-2.4 cube.

Pinned libplacebo uses zero `hdr.min_luma` as an unknown/default-black
sentinel. When the resolved semantic `Lb` is exactly zero, retain and report
`0` in the VP contract but write `PL_COLOR_HDR_BLACK` into the libplacebo
target frame. A literal zero must not silently become libplacebo's default
1000:1 black assumption.

### Strict `.cube` v1 subset

Before calling libplacebo, validate and canonicalize a documented Adobe Cube
subset:

- file size at most 64 MiB, UTF-8 (an optional UTF-8 BOM is stripped) or 7-bit
  ASCII text, with UTF-16, embedded NUL, and invalid UTF-8 rejected;
- optional blank lines and `#` comments, plus at most one optional quoted
  `TITLE` before the table; unknown or duplicate directives are rejected;
- 3D only, exactly one `LUT_3D_SIZE`, cubic dimensions 2 through 128, and the
  parsed `N^3` allocation must remain within the existing resource bound;
- exactly `N^3` RGB rows in Adobe R-fastest ordering;
- finite numeric values only, with all table outputs in `[0,1]` for v1;
- zero or one `DOMAIN_MIN` and zero or one `DOMAIN_MAX`; if present their values
  must be exactly `0 0 0` and `1 1 1` respectively;
- decimal, sign, and `e`/`E` exponent forms accepted only where the VP validator
  can canonicalize them unambiguously;
- no combined 1D shaper, extra rows, missing rows, extrapolation contract, or
  non-default domain; and
- tetrahedral interpolation with input coordinates clamped to `[0,1]`.

Reject unsupported syntax or semantics with a specific reason. Do not allow
libplacebo's permissive/narrow parser behavior to silently reinterpret a file.
An accepted cube's first RGB row must map `(0,0,0)` to exact `0.0` in all three
channels after canonical numeric parsing. This keeps an R10 code-zero picture
black aligned with bypassed code-zero borders.

### Resource and failure lifecycle

- On path, file identity, content, profile, output, device, HDR/Advanced Color,
  ICC-compatibility, or carrier-generation change, parse and validate a
  complete candidate off-side before changing the active attachment.
- Reload is explicit or profile/configuration-triggered, not per-frame polling.
  Reapplying configuration must detect a same-path content replacement. A
  permanent stale path cache is not acceptable.
- If only the file content changed under the same resolved contract and the
  replacement fails, retain the last-known-good LUT as
  `Active` and report its signature alongside the separately rejected
  candidate. If the output/profile/monitor contract changed, validate the
  candidate off-side first, then at the frame-boundary commit detach the old
  attachment before committing the changed contract. A valid candidate becomes
  active in that commit; an invalid candidate commits a no-LUT state. Do not
  discard last-known-good merely to attempt file I/O or parsing.
- Runtime render failure changes the state to `RuntimeRejected`, detaches and
  frees the failing LUT safely, restores ordinary render/dither parameters,
  and retries without a cube on the next eligible frame without a renderer
  reset loop. A same-frame second render is not implied.
- Device recreation and teardown free each resource once. libplacebo's content
  signature may invalidate dependent GPU resources; a full renderer or
  presenter reset is not required solely for a valid cube change.

## Implementation plan

1. Introduce a resolved target-calibration contract/value object and pure
   resolver/validator functions. Separate target-match, the exact
   `CarrierEvidence` predicate, and state decisions. Preserve
   explicit-versus-inherited white/black intent, including the semantic-zero
   `PL_COLOR_HDR_BLACK` adapter.
2. Enforce the initial supported matrix—Rec.709 or SDR BT.2020, full range, and
   the explicitly supported transfers—at one resolver boundary. Remove or mark
   unavailable combinations that cannot activate, including P3, limited range,
   PQ/HLG target cubes, and external HDR tone-map LUTs.
3. Add explicit LUT reference black configuration and surface all resolved
   fields through profile/rule inheritance, live configuration, inventory,
   debug log, and Ctrl+I/status OSD.
4. Add the strict cube pre-validator/canonicalizer, detailed rejection codes,
   neutral-black check, content fingerprint, and atomic candidate swap.
5. Refactor renderer LUT state so parsing alone cannot alter dither. Attach
   only an `Active` contract, and restore the selected normal render parameters
   on contract or runtime rejection.
6. Invalidate/re-resolve on every relevant profile/output/device generation
   and on same-path content replacement when configuration is reapplied.
7. Update the sample configuration, configuration UI/inventory, and
   `CONFIGURATION.html`. Clearly label v1 as full-range, video-picture-only,
   target/display calibration and document required OS/display state.
8. Add the independent CPU/GPU/lifecycle validation below, then complete an
   x64 Release build. Real-display measurement is final acceptance evidence,
   not a reason to redesign the established backbuffer path.

## Validation plan

### Deterministic automated proof

- Build an independent double-precision tetrahedral evaluator; do not use
  libplacebo as both implementation and oracle.
- Test identity and asymmetric channel/order cubes, all six tetrahedral
  regions, vertices, edges, neutral ramps, and `-epsilon`/`1+epsilon` clamp.
- With dither disabled, compare WARP/GPU R10 output with the independent oracle
  within one code per channel, with exact black and white endpoints where the
  fixture requires them.
- Probe full-range codes `0`, `1`, `64`, `512`, `940`, `1022`, and `1023` and
  prove that a limited/studio LUT profile is rejected.
- Add BT.1886 CPU reference vectors for `Lb/Lw = 0/100` and `0.005/100` at
  `V = 0, 0.018, 0.18, 0.5, 1`, with error no greater than `1e-6` normalized or
  `1e-5` relative.
- Add pure-G22 CPU vectors for the same `Lb/Lw` and `V` sets using
  `L = Lb + (Lw - Lb) * V^2.2`, plus a target-boundary GPU probe. Do not assume
  pinned libplacebo's nonzero-black gamma behavior without this proof.
- Prove target primaries/transfer/range/white/black and gamut/tone policy at
  the LUT boundary for accepted Rec.709 and SDR BT.2020 profiles, and prove
  P3-D65 and PQ/HLG target-cube profiles fail closed before attachment.
- Prove Rec.709 source -> SDR BT.2020 target -> cube and HDR BT.2020/PQ/HLG
  source -> tone/gamut-mapped SDR BT.2020 target -> cube. A BT.2020-input cube
  must receive BT.2020 RGB coordinates, not Rec.709 RGB or unconverted PQ/HLG.
- For independent linear-light conversion proof, test the Rec.709 basis vectors
  against the Rec.709-to-BT.2020 matrix
  `[0.627403896,0.329283038,0.043313066;`
  `0.069097289,0.919540395,0.011362316;`
  `0.016391439,0.088013308,0.895595253]` within `5e-6`, and prove neutrals
  remain neutral before target transfer encoding.
- Use a seam-probe cube that maps black to an obvious tint in test-only code to
  prove the video picture changes while final overlays and outer borders do
  not. Production acceptance still requires the neutral-zero black rule.
- Cover default-domain acceptance and every strict-subset rejection, including
  non-default domain, NaN/Inf, extra/missing rows, mixed 1D+3D, out-of-range
  table values, unsafe dimensions, and syntax canonicalization.
- Cover cross-profile switches, Advanced Color/HDR detection and external-
  correction attestation states, carrier and device generation changes,
  same-path replacement,
  invalid replacement, runtime render failure, no-LUT retry, error-diffusion
  disable/restore, teardown, and device recreation. Prove the guard; a warning
  in documentation alone is not acceptance.
- Cover Rec.709/BT.2020 profile switching, top-level VP-owned R10 flip versus
  child/composed rejection, direct-delivery attestation, manual display-mode
  attestation, NVIDIA SET/readback-only, analyzer-backed signaling, signaling
  failure, and restoration on exit.
- Make known exporter fixtures mandatory test assets or explicit named test
  inputs; a silently skipped environment-variable test is not acceptance.

### Calibration and real-output proof

- Measure with VP OSD/statistics/diagnostic overlays disabled and record which
  subtitle path is active.
- Record cube provenance/exporter, size, checksum, authoring primaries,
  transfer, full range, `Lw`, `Lb`, display/native mode, OS HDR/Advanced Color,
  ICC compatibility, ICC/MHC/VCGT state, GPU format/range, display bit depth,
  and VP resolved contract.
- Measure black/near-black, white/near-white, neutral tracking, primary and
  secondary colors, color-checker or equivalent patches, and at least one
  gamut-stress set. Compare no-LUT, identity, and calibration cubes.
- Use authoritative backbuffer/readback evidence to prove VP's stage and code
  output, then an independent probe/capture/measurement path to validate the
  actual display result. Do not infer calibration correctness from visual
  program material or backbuffer readback alone.
- Record tolerances before measuring. Final acceptance requires no unexplained
  systematic range remap, double transfer, double gamut transform, clipping,
  channel swap, stale cube, or calibrated-picture/border black discontinuity.
- For BT.2020, independently verify RGB Full link encoding plus the projector's
  actual BT.2020/native mode or HDMI BT.2020 signaling. A P709 DXGI carrier
  label, NVAPI SET result, or renderer target log alone is insufficient.
- HDMI analyzer/capture evidence records RGB, 10 bpc, full range, BT.2020 AVI
  colorimetry, code preservation, and SDR rather than HDR/PQ display mode.
  Meter evidence records D65, grayscale/EOTF, ColorChecker-class patches,
  saturation sweeps, and high-saturation gradients through the exact path.

## Acceptance criteria

- Every active cube has one logged, OSD-visible, fully resolved input contract:
  role/scope, file/signature, primaries, transfer, full range, white, black,
  tone/gamut policy, native profile, exact carrier identity/model/encoding,
  format/effective depth, Advanced Color state, and operator attestation.
- `AUTO` fields resolve deterministically and are never contract wildcards.
- Rec.709 and SDR BT.2020 LUT-input profiles activate only under their exact
  full-range target, transfer, luminance, carrier, and display-mode contracts.
  BT.2020 is not inferred from source mastering metadata and never implies PQ.
- BT.2020 diagnostics separately report `LUT input=FULL BT2020/<TRC>`,
  `DXGI carrier=P709`, `HDMI/display mode=BT2020`, and the authority/evidence
  for each. Post-LUT native-drive values are not mislabeled semantic BT.2020.
- P3-D65 and PQ/HLG target-cube profiles are explicitly unavailable and fail
  closed before attachment.
- A BT.1886 cube activates only with explicit matching authoring white/black;
  pure gamma 2.4 remains a separately named contract.
- Limited/studio LUT profiles and unsupported cube semantics fail closed while
  ordinary playback continues with an actionable reason.
- The accepted cube operates exactly once at target/native video-picture
  stage, followed by ordinary final video dither and no unintended color
  transform.
- Parsed/rejected/runtime-failed cubes cannot leave incompatible dither state,
  stale LUT attachment/reference, repeated render failures, or misleading
  active status. Libplacebo may legitimately retain unreachable cached objects.
- Same-path replacement is detected on configuration reapply. A failed
  same-contract replacement leaves the prior signature visibly active and the
  candidate visibly rejected; a changed-contract failure cannot retain or
  masquerade as the prior calibration.
- Deterministic parser, contract, CPU oracle, GPU readback, lifecycle, x64
  Release build, and recorded real-display validation all pass.
- Existing no-LUT playback and supported VP-0011 safe fallback remain intact.

## Non-goals

- No presenter/backbuffer redesign, arbitrary shader/LUT chain, creative look
  LUT, source normalization LUT, main/conversion LUT, ICC profile generator,
  1D shaper, or whole-frame LUT in this story.
- No P3-D65, PQ/HLG target cube, external HDR tone-map LUT, or limited/studio-
  range LUT activation until each separate target/carrier, stage, or
  domain/remap contract is approved and proven.
- No PC-madVR `.3dlut` import or claim of PC-madVR video-level file/range
  compatibility; VP v1 accepts the documented full-range `.cube` contract.
- No claim that MPC-family precedent, a DXGI label, GPU “Full” setting, or
  backbuffer readback alone proves physical-display calibration.
- No automatic projector/display-mode control and no overwrite of deployed
  user configuration or user LUT files.
- No libplacebo upgrade unless a separate dependency story approves it.

## Dependencies and disposition

- VP-0011 and VP-0012 remain the completed loader/placement foundation.
- VP-0096 supplies accepted source/range conversion groundwork.
- VP-0125, VP-0127, and VP-0133 contain the relevant presenter, delivery, and
  authoritative backbuffer evidence; this story does not repeat their design.
- VP-0100 and VP-0101 are superseded and moved to Will Not Do with this story.
- VP-0029, VP-0047, and VP-0048 remain historical inputs and are not separate
  implementation work.
- Coordinate with VP-0051 for any future generic shader-chain stage, VP-0109
  for pure-2.2 studio transport, VP-0128 for option inventory, and VP-0141 for
  live configuration reapplication. None changes the full-range v1 boundary.

## Comparative and standards references

- [mpv target-LUT option](https://github.com/mpv-player/mpv/blob/e8673660ab7ee5d4ea8f93e4bf3a6e170ab2a19a/DOCS/man/options.rst#L7604-L7608)
- [madshi on madVR LUT input-gamut preconversion](https://forum.doom9.org/archive-perm/index.php/t-146228-p-1117.html)
- [madshi on the PC madVR video-level `.3dlut` boundary](https://forum.doom9.org/archive/index.php/t-146228-p-390.html)
- [ArgyllCMS madVR calibration workflow](https://www.argyllcms.com/doc/Scenarios.html)
- [Pinned VP libplacebo target encoding and native LUT order](https://github.com/billslack2/libplacebo/blob/c3a3d203/src/renderer.c#L2800-L2825)
- [Pinned VP libplacebo cube parser](https://github.com/billslack2/libplacebo/blob/c3a3d203/src/shaders/lut.c#L30-L145)
- [Pinned VP libplacebo dither and overlay order](https://github.com/billslack2/libplacebo/blob/c3a3d203/src/renderer.c#L2945-L3015)
- [MPC-BE ICC LUT construction](https://github.com/Aleksoid1978/MPC-BE/blob/6c07f2cec684ae3a4433b1391a224a053e5803d4/src/filters/renderer/VideoRenderers/DX9RenderingEngine.cpp#L1699-L1875)
- [MPC-BE final LUT/dither shader](https://github.com/Aleksoid1978/MPC-BE/blob/6c07f2cec684ae3a4433b1391a224a053e5803d4/src/Shaders/Transformation/final_pass.hlsl#L17-L37)
- ITU-R BT.1886, Reference electro-optical transfer function for flat panel
  displays used in HDTV studio production.
- [ITU-R BT.2020](https://www.itu.int/rec/r-rec-bt.2020/en)
- Adobe Cube LUT Specification, version 1.0.
- Microsoft DXGI color-space and Windows Advanced Color/MHC documentation.
- [Microsoft Advanced Color and ICC profile behavior](https://learn.microsoft.com/windows/win32/wcs/advanced-color-icc-profiles)
- [Microsoft HDR/Advanced Color swapchain guidance](https://learn.microsoft.com/windows/win32/direct3darticles/high-dynamic-range)

## Source references

- `src\VideoProcessor-Lib\vprenderer\LibplaceboDisplayLut.cpp`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboDisplayLut.h`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboVideoRenderer.cpp`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboVideoRenderer.h`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboOutputPolicy.cpp`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboRenderParameters.h`
- `src\VideoProcessor-Lib\vprenderer\LibplaceboRenderParameters.cpp`
- `src\VideoProcessor-Lib\RendererProfileConfig.h`
- `src\VideoProcessor-Config\ConfigEditorWindow.cpp`
- `src\VideoProcessor-Test\LibplaceboLutParserTests.cpp`
- `src\VideoProcessor-Test\LibplaceboOutputPolicyTests.cpp`
- `src\VideoProcessor-Test\LibplaceboRenderParametersTests.cpp`
- `src\VideoProcessor-ConfigTests\ConfigEditorWindowTests.cpp`
- `src\VideoProcessor-VPRenderer\VideoProcessor-VPRenderer.vcxproj`
- `src\VideoProcessor-Test\VideoProcessor-Test.vcxproj.vcxproj`
- `3rdparty\libplacebo\README.txt`
- `CONFIGURATION.html`
