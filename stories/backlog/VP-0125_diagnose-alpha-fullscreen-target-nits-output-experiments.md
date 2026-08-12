# VP-0125: Diagnose Alpha fullscreen target-nits colour crushing and provide Output Experiments

## Status

Backlog (2026-08-12). A beta report identifies a fullscreen-only Alpha output
failure that needs bounded diagnosis before any production tone-mapping change.
With a BT.2020 SDR target, BT.2020 display reporting enabled, and output gamma
2.2 selected, raising `sdr_target_nits` above 200 causes visibly crushed
colours in fullscreen. The same setting does not show the reported crushing in
windowed mode. At 200 nits or below, the output still differs somewhat from
the reporter's madVR and XGIMI tone-mapping references.

No log, exact build identifier, source EOTF, source frame, active display-rule
configuration, GPU/driver state, or measurement evidence accompanied this
report. "Crushed colours" is a visual symptom, not yet a conclusion about
tone mapping, gamut conversion, levels, transfer, signalling, or the display.
No implementation branch/worktree has been selected.

Before source work starts, query the current default branch of
`billslack2/videoprocessor`, report it to the developer, and obtain explicit
confirmation of the implementation base under the tracker workflow.

## User story

As a VP Renderer beta tester tuning HDR-to-SDR output for a BT.2020 projector
mode, I need `sdr_target_nits` to produce the same coherent renderer target
and output contract in fullscreen as it does in windowed testing, so target
nits can be tuned against a reference renderer without a hidden colour crush
or a misleading preview comparison.

## Known facts and diagnostic boundaries

1. The affected setting is `sdr_target_nits`, whose accepted configuration
   range is 40 through 500 nits. There is no intended 200-nit limit in the
   configuration validation; the apparent threshold must be reproduced rather
   than encoded as a cap.
2. When `sdr_black_nits=AUTO`, its effective value is derived from
   `sdr_target_nits / PL_COLOR_SDR_CONTRAST`. Changing target nits then changes
   both target white and target black. Every threshold test must record and
   normally fix `sdr_black_nits` to one explicit value; an AUTO comparison is
   a separately labelled two-variable test.
3. For HDR input, target nits is part of libplacebo's HDR-to-SDR tone/gamut-map
   destination. For SDR input, VP intends to match source and target nominal
   luminance so normal SDR is not tone-mapped. Source EOTF and HDR metadata are
   therefore mandatory before attributing the symptom to tone mapping.
4. Alpha windowed preview and fullscreen may use different window ownership,
   swapchain/presentation models, DXGI output contracts, and display-signalling
   paths. Windowed output is a useful control, not proof of correct physical
   fullscreen output or a substitute for a matching fullscreen reference.
5. VP-0093 established that a BT.2020 render target on the known projector
   path is distinct from its P709 DXGI transport and optional NVIDIA AVI
   InfoFrame reporting. This investigation must keep target pixels, transport,
   API acceptance, signalling, application-code readback, and display response
   separate.

## Bounded diagnostic work

1. Capture an immutable reproducer: exact beta/build/commit, configuration and
   state hashes, source clip/frame/timecode, source EOTF/primaries/matrix/range
   and HDR metadata, display rule/profile, monitor, resolution/refresh,
   fullscreen/windowed state, Windows HDR/color-management state, GPU/driver,
   driver range, cable/display mode, projector mode, LUT state, and all
   renderer quality/tone/gamut/peak-detection settings. Preserve current and
   rotated debug logs before any test setting changes.
2. On the same repeatable frame, compare at least 100, 190, 200, 201, 203,
   210, 250, and 300 nits in fullscreen and windowed mode. First fix
   `sdr_black_nits`, tone-mapping method, gamut-mapping method, peak detection,
   contrast recovery, LUT state, output range/gamma, target primaries, and
   display reporting. Run `AUTO` black only after the one-variable matrix.
3. Add a generation-scoped output-and-colour-map snapshot at configuration
   application, renderer/swapchain creation, first presented frame, display
   mode transition, target-nits change, fullscreen/windowed transition, and
   teardown. It must include requested and effective target/black nits;
   source HDR facts; tone/gamut mapping functions and constants; peak state;
   LUT contract; returned libplacebo target; final VP render target; swapchain
   model/format/size; DXGI declaration and Check/Set/Check evidence; BT.2020
   reporting request/set/readback; and renderer/output generations.
4. Use deterministic HDR and SDR ramps, neutral scales, primary/secondary
   gamut stress patches, near-black, midtone, near-white, and clipping probes.
   Record R10 backbuffer values/histograms before presentation, optional HDMI
   capture/analyzer evidence, and meter/display response separately. For HDR,
   run static metadata and no/controlled peak-detection fixtures; do not infer
   physical output from an OSD label or a DXGI check.
5. Locate the first divergent stage between fullscreen and windowed output:
   source interpretation, colour-map result, target-frame metadata, target LUT,
   swapchain backbuffer, DXGI state, NVIDIA reporting, wire values, or display
   response. A valid conclusion may instead show that the comparison uses
   differing presentation/display modes; document that limitation rather than
   forcing them to match.
6. Do not change production tone mapping, gamut mapping, transfer, range,
   display reporting, or default target nits until the divergence is proven.
   Do not "fix" the report by limiting the UI to 200 nits, forcing fullscreen
   to use the preview path, or silently substituting a different output
   contract.

## Presentation architecture decision

The intended production direction is a **VP-owned DXGI presentation layer**.
Libplacebo remains the renderer: VP gives it an explicit, resolved destination
colour contract and it performs decode, scaling, colour conversion, gamut map,
tone map, dithering, and encode. VP, rather than libplacebo's convenience
D3D11 swapchain, creates and owns the normal DXGI flip-model swapchain and
its presentation lifecycle. VP must then explicitly select and verify the
backbuffer format, buffer count, swap effect, scaling/alpha flags, output,
colour-space request, HDR metadata, fullscreen transition, and `Present`.

This is not a switch to DXGI decode swapchains, D3D11 VideoProcessor, or an
alternative renderer. It separates the renderer's pixel transform from the
Windows/driver/display presentation contract:

```
libplacebo render target -> VP-owned DXGI swapchain -> Windows/driver/display
```

That separation is required for a reliable diagnosis of a fullscreen-only
failure. A libplacebo D3D11 colour hint can be mapped to a known supported
alternative and applied on a subsequent frame; that is a valid convenience
behaviour, but it is too opaque to be VP's final output-policy authority.
The renderer must render to the colour contract that VP actually resolves, not
to an earlier request or an assumed default.

For every DXGI configuration/reconfiguration VP must log the full requested,
supported, set, and active contract: adapter LUID/vendor/device/driver;
feature level; monitor/output identity; `DXGI_SWAP_CHAIN_DESC1`; colour-space
candidate list; `CheckColorSpaceSupport` flags and HRESULT; `SetColorSpace1`
HRESULT; `GetColorSpace1` readback; HDR metadata request/set/readback where
available; resize/fullscreen/Present HRESULTs; and the correlated renderer and
test-run generation. Treat a changed monitor, fullscreen transition, resize,
or Windows HDR/display state as a new presentation configuration, because
DXGI colour-space support is output-dependent.

## Output Experiments

Implement a separate **Output Experiments** section to run the controlled
matrix above. It must not be hidden among general Advanced options: these
settings change the visible renderer contract and need a focused explanation,
an effective-contract summary, and a direct recovery action. The section starts
at recommended defaults but may be retained and saved per display profile when
the developer accepts the supported subset; it is never a panel of unbounded
colour overrides.

### Allowed test dimensions

- `sdr_target_nits` (40..500) and explicit `sdr_black_nits` or a prominently
  labelled `AUTO` comparison;
- tone-mapping method, gamut-mapping method, peak detection, and contrast
  recovery;
- target primaries (Rec.709 or BT.2020) and the existing BT.2020 reporting
  request, with current NVIDIA save/set/restore safeguards intact;
- policy-valid presentation request (`auto`, `composed`, or `direct`) and
  policy-valid output range/gamma combinations, including VP-0109 diagnostic
  modes only when that spike permits them; and
- controlled fullscreen/windowed test selection plus an explicit clean
  renderer/swapchain recreation between runs; and
- named DXGI diagnostic presets: SDR backbuffer depth (8-bit versus 10-bit),
  flip-model presentation policy, and device compute-path selection. These
  are evidence-producing controls, not arbitrary DXGI descriptor editing.

### Capability inventory and configuration scope

Before adding controls, produce a version-pinned inventory of libplacebo,
D3D11, DXGI, and VP settings. For each candidate, record the upstream default,
VP's current requested/effective value, whether it is read back, its required
rebuild scope, affected tests, and one of: **production candidate**,
**Output-Experiments diagnostic**, **developer-only hardware diagnostic**, or
**not exposed**. This must include at minimum:

- D3D11 device options: adapter/LUID selection, software policy, debug layer,
  compute disablement, creation flags, feature-level bounds, and maximum frame
  latency;
- D3D11/DXGI swapchain options: flip versus blit model, format/colour and alpha
  bits, 10-bit-SDR disablement, buffer count, flags, scaling, present/tearing
  policy, output selection, and exclusive/fullscreen transition;
- renderer target contract: target primaries/transfer/levels, target and black
  nits, HDR static metadata, output range/gamma policy, display-reporting
  request, and the DXGI colour-space mapping;
- colour-map and renderer options: tone/gamut function and their constants,
  metadata selection, LUT and tone-map LUT sizes, peak detection thresholds and
  smoothing, contrast-recovery parameters, scaler/anti-ringing/deband/dither
  controls, and clipping/LUT visualization; and
- observability-only options: `pl_renderer_get_hdr_metadata`, renderer errors,
  cache/peak reset state, target-frame metadata, shader/cache generation, and
  deterministic backbuffer probes/histograms.

Do not expose all of these to normal users. The initial beta UI receives only
the named experiment dimensions and diagnostic presets above. Device debug,
adapter, feature-level, creation-flag, arbitrary DXGI flag, raw colour-space,
and arbitrary metadata changes are developer-only, restart/rebuild-gated, and
never persisted as ordinary display defaults. A later product decision may
promote only configurations that pass the controlled matrix and have a clear,
safe recommendation.

### Rebuild contract

Every Output Experiments Apply starts a fresh test generation. Initial
correctness takes priority over incremental update performance:

1. **Full D3D11 device recreation** is mandatory for adapter/LUID, software,
   debug, no-compute, creation-flag, feature-level, or device-latency changes.
   Debug-layer changes may require a process restart if Windows cannot enable
   the requested layer in-process.
2. **Renderer and VP-owned swapchain recreation** is mandatory for output
   range/transfer/primaries, target/black nits, target metadata, tone/gamut
   mapping and constants, peak detection, LUT sizing, dither/output bit depth,
   swap effect/flags, DXGI colour space, display-reporting, monitor selection,
   and every fullscreen/windowed transition. Destroy all dependent targets
   before resize/recreation; reset peak detection and renderer cache state;
   then recreate, apply the resolved colour contract, and validate readback
   before the first measured frame.
3. **No graphics rebuild** is permitted only for UI/logging/OSD changes.
   Although libplacebo can automatically recreate some cached resources on
   parameter changes, that is insufficient as the initial lab contract where
   target metadata and swapchain colour state are under investigation. An
   incremental path may be introduced later only after equivalent-run evidence
   proves it preserves pixels and the active DXGI contract.

### Guardrails

1. Output Experiments must use named, policy-validated dimensions; it must not expose raw
   DXGI colour-space IDs, arbitrary target metadata, a P2020 transport override,
   undocumented transfer pairing, or a bypass around Check/Set/Check,
   target-LUT validation, or VP-0093's P709-transport/AVI-signalling design.
2. Each Apply creates a unique test-run identifier, resets renderer state that
   can carry peak/output decisions, performs a bounded normal recreation, and
   logs requested versus resolved values. It must not reuse output acceptance,
   peak detection, or shader assumptions from an earlier test run.
3. Non-default values are visibly labelled in the UI/OSD and logs. They may be
   exported as an evidence manifest and explicitly saved as a display-profile
   override, but must not silently migrate ordinary user configuration, mutate
   the global recommended defaults, or overwrite another profile's settings.
   Unsaved changes are discarded on exit; saved overrides round-trip normally.
4. Provide a prominent **Restore recommended defaults** button for this
   section. It must show the exact values that will be restored, discard only
   the Output Experiments overrides in the selected profile after confirmation,
   recreate the renderer through the standard safe path, reset temporary peak
   and output state, restore any saved NVIDIA output state as required, and
   record the action in the log. It must not modify capture, screen/NLS,
   hotkey, LUT-file, or unrelated profile settings.
5. Invalid combinations fail before output mutation with an actionable reason.
   A failed experiment preserves the last valid configuration under VP-0103
   semantics; it never silently falls back to unrequested Full/sRGB or leaves
   BT.2020 signalling enabled after teardown.
6. Initially identify the section as experimental in beta/developer builds.
   A later release may retain only the explicitly proven controls, their
   recommendations, and the Restore button. The section has no steady-state
   render cost when no override is active.

## Acceptance criteria

1. The result either reproduces a one-variable fullscreen threshold with
   source and output facts preserved, or records why the original observation
   cannot be reproduced. It never changes the reported 200-nit boundary into
   an undocumented product restriction.
2. The evidence identifies whether the input was HDR or SDR and attributes the
   first fullscreen/windowed divergence to a concrete pipeline boundary, or
   explicitly records that available evidence cannot distinguish it.
3. Every comparison holds target black fixed for its primary matrix and labels
   any AUTO-black test as coupled target-white/black behavior.
4. Output Experiments can execute the named test matrix with isolated renderer
   generations, complete requested/effective snapshots, normal rollback and
   display-signal restoration, per-profile explicit saving, and no invalid
   low-level override path.
5. VP owns the flip-model DXGI presentation layer and validates the active
   DXGI colour space and metadata after each recreation. Libplacebo receives
   the same resolved destination contract; it is not allowed to silently
   become the final presentation-policy authority.
6. The version-pinned capability inventory classifies every relevant
   libplacebo/D3D11/DXGI option, distinguishes implemented from unimplemented
   behaviour, and assigns a rebuild scope. The initial UI exposes only the
   bounded named experiments; unsafe raw controls remain developer-only or
   unexposed.
5. Output Experiments has a prominent Restore recommended defaults action that
   restores only that section's selected-profile overrides, resets the
   renderer/state safely, and leaves every unrelated configuration field
   unchanged.
6. R10 application-code, DXGI/API, optional wire, photometric, and visual
   evidence remain distinct. The windowed preview is never presented as proof
   of the physical fullscreen output contract.
7. Any eventual production repair has focused regression coverage for HDR and
   SDR input; 100/190/200/201/203/250/300-nit cases; fixed and AUTO black;
   Rec.709/BT.2020 targets; reporting on/off; no-LUT/identity-LUT cases;
   fullscreen/windowed transitions; renderer recreation; and the established
   Auto/Full, Limited/2.4, VP-0109, VP-0093, and madVR paths.
8. A clean x64 Release build and relevant tests pass before any deployment.
   No deployment may overwrite active user configuration; any later deployment
   replaces `VideoProcessor.exe` and `vprenderer\VideoProcessorVPRenderer.dll`
   as the same successful Release-build pair and verifies their hashes.

## Non-goals

- Do not make a 200-nit cap or raw DXGI override a customer-facing output
  option. A named policy-valid experiment may be retained only with its safe
  recommendation, validation boundary, and Restore recommended defaults action.
- Do not claim that the windowed image, an API success, a backbuffer readback,
  or visual similarity to madVR/XGIMI alone proves physical output correctness.
- Do not redesign arbitrary calibrated/pixel-owned output, P3-D65 workflows,
  or 3D-LUT architecture; VP-0100 and VP-0101 own that broader work.
- Do not fold the independent Alpha ingress-range work from VP-0096 into this
  diagnosis, or change global NVIDIA/projector/Windows settings without
  explicit approval.

## Dependencies and references

- VP-0093 remains authoritative for BT.2020 target pixels, P709 DXGI
  transport, and NVIDIA AVI reporting. Preserve that separation in all Lab
  experiments.
- VP-0109-1 is proving a distinct pure-Gamma-2.2/Studio-G22 pairing with tone
  mapping disabled. Share diagnostics where useful, but do not broaden or
  block that controlled transfer proof with this target-nits investigation.
- VP-0100 supplies broader pixel-owned presentation methodology; VP-0103
  supplies safe live configuration and rollback semantics; VP-0096 owns source
  conversion/range correctness.
- Likely paths to verify on the confirmed implementation base:
  `src\VideoProcessor-Lib\vprenderer\LibplaceboVideoRenderer.cpp`,
  `LibplaceboOutputPolicy.{h,cpp}`, `LibplaceboDisplayLut.cpp`, renderer/UI
  configuration pages, `VideoProcessor.cfg`, `CONFIGURATION.html`, and the
  relevant x64 renderer/policy tests.
