# VP-0133: Capture authoritative renderer output and near-black diagnostics

## Status

In Progress (2026-08-16). The developer confirmed the newly updated default
integration branch `v1.2.001-beta` as the implementation base. Work began from
clean base `9daf9ab8e15493d77fb74d25706452eafeb8cd8a` in branch
`codex/vp0133-renderer-output-capture`, worktree
`work\vp-0133-renderer-output-capture`.

A VP Renderer beta tester reports a slightly darker
image and loss of differentiated near-black detail compared with madVR and
projector tone mapping. The existing log can distinguish requested and actual
transport, source EOTF, formatter contract, and broad R10 output statistics,
but it cannot export the rendered pixels for a controlled comparison.

Investigation against `origin/v1.2.001-beta` at `74a6c7e` also found that the
current `LogOutputReadback()` always unwraps the libplacebo compatibility
swapchain. During VP-owned Direct presentation, that is not the authoritative
backbuffer rendered and presented by VP. This story corrects that diagnostic
boundary before treating readback or screenshots as evidence.

The repository default implementation branch was manually discovered and
explicitly confirmed on 2026-08-16 as `v1.2.001-beta`. A fresh fetch advanced
the earlier observed tip from `74a6c7e` to confirmed base `9daf9ab` before the
implementation worktree was created.

The story-ID audit found no duplicate canonical root story and confirmed
VP-0132 as the highest canonical root. It also found historical index-only
root rows VP-0011 and VP-0069. Those pre-existing inconsistencies are reported
here and are not silently changed by VP-0133.

## User story

As a VP Renderer beta tester comparing black detail with madVR, I need a
one-shot capture of the exact pixels VP rendered immediately before Present,
together with the resolved source and output contract, so we can distinguish a
renderer conversion difference from DXGI, GPU, HDMI, or display behavior.

## Requirements

1. Add a configurable VP action and shortcut for **Capture rendered output**.
   The shipped default is `Ctrl+Alt+S`. It must use the normal foreground and
   modifier-safe shortcut dispatch; it must not fire while the operator types
   in another application.
2. The request is one-shot and asynchronous from the UI. It captures the next
   successfully rendered frame without rebuilding the renderer, changing
   timing policy, or blocking the UI thread on GPU or file I/O.
3. For the VP-owned presenter, capture the actual acquired
   `vpOwnedBackbuffer` after `pl_render_image` and GPU submission is complete,
   but before releasing its references and calling `Present`. Do not unwrap or
   label the libplacebo compatibility swapchain as authoritative in this path.
4. For the libplacebo-owned presenter, capture the actual current swapchain
   framebuffer before submit/swap. Both paths must record which surface and
   presenter owned the evidence.
5. Preserve R10 channel code values losslessly. Write a 16-bit-per-channel PNG
   or TIFF representation with an explicit, documented R10-to-16-bit mapping.
   Do not silently convert through Windows color management, the compositor,
   an 8-bit desktop screenshot, or the physical display transfer curve.
6. Write an adjacent UTF-8 JSON metadata file with at least: UTC/local time,
   source/frame sequence, dimensions and backbuffer format, source encoding,
   formatter/ingress path, source system/range/transfer/EOTF/primaries,
   requested and actual presentation/range/transfer/primaries, fallback or
   blocked reason, target and black nits, DXGI declaration, Check/Set/Check
   status, presenter owner/generation, display identity, renderer/plugin build,
   and SHA-256 of the image.
7. Save under an installation-relative `screenshots` directory by default.
   Use collision-safe filenames and create the directory when needed. Failure
   must be logged and shown briefly in the OSD without interrupting video.
8. Add near-black diagnostics to the same captured frame. Record exact or
   bounded histograms around full-range black and 10-bit studio black, counts
   below/at/above 64, and representative RGB code values. Broad min/max/mean
   statistics alone are insufficient to diagnose crushed shadow steps.
9. Keep input and output semantics separate. A later optional raw/formatted
   ingress capture may share the action, but it must use a distinct file and
   metadata role; it must never be described as the rendered output.
10. Do not claim that an application readback proves GPU output dynamic range,
    HDMI InfoFrames, the projector transfer curve, or measured luminance. The
    metadata and documentation must explain that matching renderer captures
    move the remaining investigation downstream to transport/display evidence.
11. Add an explicit renderer-profile `sdr_adjust_gamma` policy with three UI
    choices: **Auto (mpv-compatible)**, **On - color managed (current
    behavior)**, and **Off - suppress SDR transfer conversion**. A missing key
    resolves to On so existing users do not change behavior silently.
12. Implement this as source/target transfer metadata policy at the libplacebo
    frame boundary, not as an artistic gamma control. On retains the declared
    SDR source transfer. Auto suppresses conversion only for an automatic sRGB
    target and a common ambiguous BT.1886, pure-2.2, or sRGB source. Off treats
    SDR as already encoded for the finalized accepted SDR target transfer.
13. The policy applies only to SDR input. It must not rewrite PQ, HLG, or other
    HDR input, must use the finalized actual output after fallback, and must not
    falsify the DXGI declaration or accepted output target.
14. UI help, OSD, ordinary logs, and rendered-output JSON must distinguish
    configured/declared/effective source transfer, accepted target transfer,
    ADJUST/SUPPRESS/NOT_APPLICABLE/BLOCKED action, and reason. They must state
    that matrix/range/primaries conversion, scaling, LUTs, shaders, dithering,
    quantization, and physical display response remain active.

## Logging additions

- Log screenshot request, accepted frame sequence, presenter path, generation,
  authoritative surface identity, GPU synchronization/copy result, encoding
  result, filenames, byte counts, hashes, and completion latency.
- On each relevant renderer generation, log the active per-renderer input
  conversion and the resolved source `pl_color_repr`/`pl_color_space` next to
  the final output contract.
- When a selected output gamma cannot be represented by policy, keep the
  existing OSD fallback and add an explicit log stating that the requested
  gamma did not alter rendered pixels. This is especially important because
  the editor currently lists power-gamma values that the output policy does
  not implement for every range/transport combination.
- Correct output-diagnostic readback so VP-owned evidence always comes from the
  VP-owned presented backbuffer. Retain the existing summary statistics for
  normal diagnostic runs, now sourced from the correct surface.
- Emit one structured `SDR_GAMMA` record when the resolved decision or relevant
  processing caveat changes, rather than spamming every frame. Include the
  requested policy, applicability/action, configured and declared/effective
  source, requested/actual target, output fallback state, and other active
  processing caveats.

## Acceptance criteria

1. Pressing the configured shortcut once produces exactly one image and one
   metadata file from the next successful frame; holding or auto-repeat does
   not create an uncontrolled capture stream.
2. The default `Ctrl+Alt+S` works with VP in the foreground and does not fire
   from ordinary background typing. A custom shortcut round-trips through the
   configuration editor and runtime parser.
3. A known R10 test image containing code values around 0 and 64 round-trips
   through the saved lossless representation with exact documented values.
4. Tests prove that VP-owned capture uses `vpOwnedBackbuffer`, happens after
   render/flush and before release/Present, and never falls through to the
   compatibility swapchain. The libplacebo-owned path uses its actual frame.
5. Full and Limited captures report correct separate histograms. Full-range
   values below 64 are not automatically classified as an output failure;
   Limited evidence records excursions without inventing display behavior.
6. Capture failure is bounded, logged, visible, and recoverable. Video
   presentation continues and a later request can succeed.
7. Focused policy, shortcut, metadata, image-encoding, and presenter-ordering
   tests pass, followed by a clean x64 Release build and the established native
   and configuration suites.
8. A tester package includes the feature, configuration documentation, the
   default shortcut, and concise instructions for capturing the same SDR
   Rec.709 PLUGE/ramp frame in madVR and VP Renderer.
9. Policy tests cover On/default compatibility, Off against accepted sRGB,
   pure-2.2 and pure-2.4 targets, mpv-compatible Auto boundaries, unknown or
   unsafe outputs, and HDR exclusion. UI tests cover exact labels and
   save/reload; applying the setting performs a renderer restart, not the full
   capture teardown reserved for DXGI Output Experiments.

## Non-goals

- Do not treat a desktop screenshot as physical wire or projector proof.
- Do not add continuous video recording or capture every frame.
- Do not change production output range, gamma, tone mapping, LUT, or projector
  recommendations in order to make two screenshots look similar.
- Do not make screenshot capture depend on the active-output sweep harness.
- Do not describe Off as bit-exact passthrough or a complete madVR mode.

## Dependencies and references

- VP-0125 owns the experimental VP-owned DXGI output path and requested versus
  actual transport diagnostics.
- VP-0126 owns the standalone Alpha test-pattern generator and can provide
  known ramps/patches for validation.
- VP-0127 owns output-format and composed-delivery evidence.
- VP-0130 owns foreground-safe shortcut behavior and per-renderer input
  configuration clarity.
- libplacebo exposes host texture transfer through `pl_tex_download`; the
  Windows VP-owned implementation may instead use an explicitly synchronized
  D3D11 staging copy of the already acquired authoritative backbuffer.
- mpv implements `sdr-adjust-gamma` in its frontend by changing frame transfer
  metadata before libplacebo rendering; libplacebo has no equivalent one-switch
  option. MPC Video Renderer similarly provides precedent for ordinary Rec.709
  SDR without an extra transfer remap, but neither precedent establishes a
  bit-exact or physical display contract.

## Implementation and verification record (2026-08-16)

Implemented in VideoProcessor commit `b3e6990` on pushed branch
`codex/vp0133-renderer-output-capture`, based directly on the freshly fetched
`origin/v1.2.001-beta` commit `9daf9ab`.

- Added the configurable `capture_rendered_output` action. The default is
  `Ctrl+Alt+S`; the originally considered `Ctrl+Shift+S` was rejected because
  it already opens Configuration.
- Captures the authoritative VP-owned R10 backbuffer and writes an asynchronous
  WIC 64-bpp RGBA PNG plus JSON sidecar under `screenshots`. The documented
  reversible mapping is `(r10 << 6) | (r10 >> 4)`; the exact original code is
  recovered with `r16 >> 6`.
- The sidecar hashes the tightly packed original R10 samples with SHA-256. This
  is a stronger identity check for renderer code values than hashing the PNG
  container, whose encoder metadata and layout are not part of the pixel
  contract.
- Metadata records ingress/source semantics, requested and resolved output
  contract, presenter and swapchain generation, DXGI Check/Set/Check evidence,
  tone/gamut controls, full-frame min/max/excursions, and explicit near-black
  buckets. Logs distinguish authoritative-present backbuffer evidence from the
  compatibility swapchain and explicitly report output-gamma policy fallbacks.
- Added exhaustive R10-to-16-bit round-trip coverage, near-black bucket tests,
  and shortcut default/save/reload coverage. The renderer plugin ABI advanced
  from 8 to 9 so mismatched host/plugin pairs fail cleanly.

Verification used a clean x64 Release rebuild from committed `b3e6990` and
reported `VERSION_DESCRIBE v1.2.001-beta-b3e6990` with
`VERSION_DIRTY=false`. Focused output-policy tests passed 40/40 and the
configuration-editor test executable passed. The full native run passed
826/831. Its five failures reproduce independently and are pre-existing
configuration-schema/reference baseline failures unrelated to VP-0133:

- `ConfigEditorCoreRoundTripsEveryEditorOwnedKey`
- `Vp0079OwnerVariantsResolveWithoutPersistedProfileState`
- `Vp0097NamedViewportsUseFileOrderAndIgnoreLabels`
- `ConfigurationReferenceMatchesPublicFieldInventory`
- `ConfigEditorCoreValidatesEveryEditableOrderedProfileSurface`

The canonical Release staging passed its 55-file manifest check. Two verified
archives were produced: the regular beta package and a separate VP-0133 tester
package containing the capture guide and active-output sweep tools. Neither
contains an active `VideoProcessor.cfg`; only `VideoProcessor.cfg.example` is
included. Live projector comparison and external measurement remain open, so
the story stays In Progress rather than claiming that application readback has
validated HDMI or physical display behavior.

The subsequent SDR comparison audit added the reviewed `sdr_adjust_gamma`
policy to this same story rather than creating a competing gamma story. The
implementation keeps the accepted output/DXGI target authoritative and changes
only effective SDR source transfer metadata at the per-frame libplacebo
boundary. The UI exposes Auto/On/Off with explicit transfer-only caveats; the
ordinary log, renderer status/OSD, and VP-0133 JSON sidecar expose the resolved
decision. Focused policy/restart tests pass 45/45 and the configuration/UI suite,
including exact label and save/reload coverage, passes. The full native suite
remains at the same five documented pre-existing failures with no new failure.

This extension is committed and pushed as `bedc3f0` (implementation/tests/docs)
and `424ff92` (shipped example configuration), still based on the freshly
verified default-branch tip `9daf9ab`. A serial clean x64 Release rebuild from
`424ff92` completed with `VERSION_DESCRIBE v1.2.001-beta-424ff92` and
`VERSION_DIRTY=false`. The final native result is 831/836 with exactly the same
five baseline failures listed above; the configuration/UI executable passes.
The 55-file canonical manifest and both regular and VP-0133 tester archives
were rebuilt from that commit. The archives contain only
`VideoProcessor.cfg.example`, never an active `VideoProcessor.cfg`; the tester
archive additionally contains the capture guide and active-output sweep tools.
