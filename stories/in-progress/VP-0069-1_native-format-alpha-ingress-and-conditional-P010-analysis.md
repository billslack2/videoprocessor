# VP-0069-1: Native-format Alpha ingress and conditional P010 analysis

## Status

In progress. Parent: VP-0069.

Implementation started 2026-08-02 from `origin/v1.1.015-beta` commit
`d6dbd8bd135e51c36cae5fc9e2013562808fcf13`, on local branch
`codex/vp0069-1-native-alpha-ingress` in the clean linked worktree
`C:\\Users\\bslac\\vp\\videoprocessor-vp0069-1`.

### Initial ingress trace (2026-08-02)

- The capture callback retains and queues the original `VideoFrame` buffer and
  its `VideoState`; no conversion occurs at queue admission.
- On the render thread, `RenderLocked` unconditionally creates/reuses a P010
  formatter result before NLS, scene detection, subtitle placement, and the
  two-plane libplacebo upload.
- This beta has no `VideoFrameEncoding::P010` source value. The supported
  Alpha inputs currently accepted by the formatter are v210, UYVY, ARGB/BGRA,
  R10b/R10l, and R12L. `P010` is the renderer's intermediate format, not a
  declared capture encoding.
- The first native candidate must therefore preserve the native source buffer
  through upload and provide a bounded luma adapter (or an explicit analysis
  fallback) before it can remove this conversion safely.

### Initial implementation (2026-08-02)

- The Alpha OSD now omits the inactive DirectShow Start/Stop method and always
  reports the capture frame offset. The offset is applied to the capture
  timestamp before either renderer consumes a frame.
- Latency rows are normalized across backends as `Renderer`, `Presentation`,
  and `Total`. DirectShow presentation is the remaining lead to its requested
  sample PTS; Alpha presentation is the predicted lead from its latest submit
  to the next DXGI display-sync target. `Total` is the video delay relative to
  the audio-extraction/shared capture-clock baseline, including the configured
  video frame offset. It does not claim physical panel processing or scanout
  latency, which VP cannot observe.
- Removed the Alpha-only `alpha_queue_size` setting. An explicit
  `[queue] steady_reserve_frames` now selects Alpha's desired FIFO depth via
  the same queue-policy call used by DirectShow; the retired key is rejected
  by configuration validation.
- Added a reversible ARGB/BGRA native RGB upload path. It is selected only
  when P010 is not explicitly requested and no P010-only analysis consumer
  (NLS, scene analysis, or scope subtitle analysis) is active. The OSD shows
  the resolved ingress path. Selecting P010 forces the established converter;
  all other formats remain on the tested P010 fallback pending their own
  native representation and bounded-analysis validation.
- Verified by a successful x64 Release solution build and all 455 existing
  unit tests.

## User story

As an Alpha-renderer user, I want VP to retain and upload supported captured
formats natively instead of always converting the complete frame to P010, so
the renderer can remove avoidable CPU work and latency without sacrificing
color correctness or the picture/subtitle analysis VP depends on.

## Current behavior and problem

The Alpha/libplacebo path currently performs an unconditional CPU conversion
to P010 before libplacebo upload/rendering. The converted P010 frame is also
the assumed input for active-picture/black-bar evidence, NLS, and related
analysis. That is a simple common contract, but it can impose a full-frame CPU
copy/conversion even when the captured format has a safe native D3D11/
libplacebo upload path.

This story must not assume that avoiding P010 is automatically faster. A GPU
conversion followed by CPU readback, implicit staging resource, or extra queue
would be a regression. The required result is the lowest measured end-to-end
latency with a correct image and correct analysis; P010 remains a supported,
explicit fallback.

## Scope

1. Inventory relevant captured `VideoFrameEncoding` formats and the exact
   D3D11/libplacebo upload representation available for each.
2. Establish one Alpha ingress capability table: source encoding/bit depth/
   chroma/range, native upload eligibility, needed conversion, analysis
   representation, and P010 fallback reason.
3. Preserve original format and color metadata through Alpha admission until a
   selected native or fallback conversion consumes it. Do not reinterpret
   range, matrix, transfer, chroma location, field data, or HDR metadata.
4. Implement native upload only for proven device/library combinations. Retain
   P010 for unsupported formats and log the fallback once per generation.
5. Make black-bar/active-picture detection, NLS aspect evidence,
   subtitle/glyph region detection, and scene-analysis input format-aware.
6. Prefer source-native luma access or a bounded downsampled luminance
   extraction. Do not create a full-frame P010 buffer merely to inspect bars
   or subtitles.

## Required black-bar and subtitle analysis contract

madVR's P010/P016/NV12 limitation is not a VP requirement. Prove VP's own
requirements independently for every retained native Alpha format.

- Active-picture/black-bar detection must produce equivalent trusted rectangle,
  confidence, and rejection behavior to the P010 reference, within documented
  format-specific tolerances.
- NLS must receive equivalent evidence and remain safe through aspect changes,
  dark scenes, UI, fades, credits, and high-black artwork.
- Subtitle/glyph detection must identify its capture region for native formats,
  including entirely-in-bar, multi-line, and bar-boundary subtitles. It may use
  derived luma/contrast but must not require OCR or full-frame P010.
- If a format cannot support trusted analysis, keep native rendering only when
  safe and explicitly report analysis unavailable, or select P010 fallback.
  Never fabricate trusted bar/subtitle evidence.
- OSD/logging must state source encoding, upload path, analysis representation,
  and fallback/unavailable reason.

## Non-goals

- No blanket GPU-pipeline rewrite or GPU-to-CPU readback design.
- No change to DirectShow/madVR format negotiation.
- No new subtitle relocation/OCR implementation; this supplies the analysis
  ingress required by VP-0070.
- No removal of the proven P010 path until native paths qualify.

## Required sequence

1. Trace current capture -> formatter -> P010 analysis -> upload -> submission
   ownership, copies, allocations, locks, queue crossings, and metadata.
2. Build the capability table and synthetic corpus for UYVY/v210/P010 and each
   supported packed RGB format.
3. Prototype one reversible native path; compare color/range/HDR output with
   P010 reference.
4. Add the smallest luminance/analysis adapter needed by active-rectangle and
   subtitle/glyph consumers, off UI and presentation hot paths.
5. Measure CPU, allocations, upload, render/present, VP queue residence, and
   physical capture-to-visible latency against P010 at 23.976 and 59.94/60,
   SDR and HDR where applicable.
6. Retain native paths only with net measured benefit or a correctness reason;
   otherwise keep and document P010 fallback.

## Verification

1. Unit-test capability selection and metadata propagation for every path.
2. Compare native/P010-reference output for SDR Rec.709, SDR BT.2020, HDR/PQ,
   full/limited range, and relevant packed RGB inputs.
3. Validate active-rectangle/NLS behavior on 16:9, scope, 4:3, mixed-aspect,
   dark, fade, UI, and high-black-artwork content.
4. Validate subtitle/glyph capture-region evidence for single-line, multi-line,
   entirely-in-bar, and bar-boundary subtitles.
5. Exercise renderer starts, refresh changes, HDR/SDR transitions, and
   rebuilds. No queue growth, stale-frame flash, drop burst, or analysis leak.
6. A native path is accepted only if physical and stage-level latency is equal
   to or better than P010; a lower conversion benchmark alone is insufficient.

## Acceptance criteria

- Alpha no longer unconditionally converts every supported source format to
  P010 before rendering.
- Every accepted native path has verified color/metadata correctness and a
  documented bounded analysis representation.
- Black-bar/NLS and subtitle/glyph-region detection work without full-frame
  P010 for accepted formats, or report a deliberate, truthful fallback.
- P010 remains a tested safe fallback with no silent behavior change.
- No hidden GPU readback, extra queue, or presentation-latency regression is
  introduced.

## Dependencies and references

- Parent VP-0069; VP-0021; VP-0023; VP-0070.
- `src\\VideoProcessor-Lib\\vprenderer\\LibplaceboVideoRenderer.cpp` and the
  current formatter/active-picture analysis path, to confirm during work.
