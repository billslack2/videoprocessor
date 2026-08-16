# VP-0133: Capture authoritative renderer output and near-black diagnostics

## Status

Backlog (2026-08-16). A VP Renderer beta tester reports a slightly darker
image and loss of differentiated near-black detail compared with madVR and
projector tone mapping. The existing log can distinguish requested and actual
transport, source EOTF, formatter contract, and broad R10 output statistics,
but it cannot export the rendered pixels for a controlled comparison.

Investigation against `origin/v1.2.001-beta` at `74a6c7e` also found that the
current `LogOutputReadback()` always unwraps the libplacebo compatibility
swapchain. During VP-owned Direct presentation, that is not the authoritative
backbuffer rendered and presented by VP. This story corrects that diagnostic
boundary before treating readback or screenshots as evidence.

The repository default implementation branch was manually discovered on
2026-08-16 as `v1.2.001-beta`. Source implementation must not begin until the
developer explicitly confirms that branch under the tracker implementation
gate.

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
   The shipped default is `Ctrl+Shift+S`. It must use the normal foreground and
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

## Acceptance criteria

1. Pressing the configured shortcut once produces exactly one image and one
   metadata file from the next successful frame; holding or auto-repeat does
   not create an uncontrolled capture stream.
2. The default `Ctrl+Shift+S` works with VP in the foreground and does not fire
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

## Non-goals

- Do not treat a desktop screenshot as physical wire or projector proof.
- Do not add continuous video recording or capture every frame.
- Do not change production output range, gamma, tone mapping, LUT, or projector
  recommendations in order to make two screenshots look similar.
- Do not make screenshot capture depend on the active-output sweep harness.

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
