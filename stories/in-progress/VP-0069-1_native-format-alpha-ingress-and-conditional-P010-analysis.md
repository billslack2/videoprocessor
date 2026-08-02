# VP-0069-1: Native-format Alpha ingress and conditional P010 analysis

## Status

In Progress. Parent: VP-0069.

Rebased 2026-08-02 onto `origin/v1.1.015-beta` commit
`7f1ae2f`, on local branch
`codex/vp0069-1-native-alpha-ingress` in the clean linked worktree
`C:\\Users\\bslac\\vp\\videoprocessor-vp0069-1`.
Latest source commit: `20acbdd` (`fix(alpha): remove obsolete queue override`).

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

- The Alpha OSD omits the inactive DirectShow Start/Stop method. Frame offset
  is now deliberately disabled for Alpha (the preserved DirectShow value is
  restored when switching back): Alpha's immediate FIFO does not schedule
  presentation from capture PTS, so applying an offset only changed a
  diagnostic timestamp and provided no present benefit.
- Latency rows are normalized across backends as `Renderer`, `Presentation`,
  and `Total`. DirectShow presentation is the remaining lead to its requested
  sample PTS; Alpha presentation is the predicted lead from its latest submit
  to the next DXGI display-sync target. DirectShow `Total` is the video delay
  relative to the audio-extraction/shared capture-clock baseline, including
  the configured video frame offset. Alpha `Total` is a measurement from the
  raw hardware-capture timestamp (before that offset) to its forecast display
  target, making it comparable with the audio-extraction boundary without
  changing Alpha's immediate FIFO scheduling. Neither renderer reports
  physical panel processing or scanout latency.
- Removed the Alpha-only `alpha_queue_size` setting. `queue_size` remains
  Alpha's visible hard FIFO capacity (for example, 32); `[queue] target_frames`
  independently selects the lower live/prefill target (for example, 2) via the
  same queue-policy call used by DirectShow. The retired
  `steady_reserve_frames` spelling is accepted only as compatibility input; the
  retired Alpha-only key is rejected by configuration validation.
- `Total` continues to use raw hardware capture so it
  measures the audio-relative capture-to-target delay rather than an arbitrary
  timestamp adjustment. During display-rate validation, the OSD shows the
  provisional DXGI rate together with its `warming` state; it is not used by
  timing policy until accepted.
- Added a reversible ARGB/BGRA native RGB upload path. It is selected only
  when P010 is not explicitly requested. P010-only consumers (NLS, scene
  analysis, and scope subtitle analysis) are explicitly unavailable during
  native RGB rather than silently forcing a full-frame P010 conversion. The
  OSD shows the resolved ingress path and that unavailable state. Selecting
  P010 forces the established converter; all other formats remain on the
  tested P010 fallback pending their own native representation and
  bounded-analysis validation.
- v210 is the common 10-bit 4:2:2 DeckLink source and is the next ingress
  target. It cannot be passed to `pl_upload_plane` as a normal image plane:
  four packed 32-bit words encode six pixels with changing Y/Cb/Cr semantics.
  The current P010 result is therefore accurately labelled `P010 (source
  fallback)`, not `P010 (analysis)`, when P010 was not explicitly selected.
  A valid native v210 implementation needs a GPU unpack pass from the original
  packed bytes; it must not disguise the existing CPU P010 formatter as native.
- Verified by a successful x64 Release solution build and all 456 unit tests.

### Lossless ingress tranche (2026-08-02)

- Automatic v210 ingress now unpacks to P210 instead of P010. It preserves
  every original 10-bit luma and 4:2:2 chroma sample, including independent
  chroma rows, and supports valid even widths that end in a partially used
  six-pixel v210 pack. This is lossless with respect to source samples, but is
  accurately **not** labelled raw-native: it remains a CPU packed-to-planar
  unpack plus upload. Selecting the P010 override retains the established
  P010 conversion.
- Automatic UYVY and HDYC ingress follows the same P210 route, preserving all
  8-bit 4:2:2 chroma rows. The image representation declares the source as
  8-bit with an eight-bit packing shift, rather than incorrectly presenting it
  as newly created 10-bit content. P010 remains the explicit override.
- ARGB/BGRA and regular 32-bit packed RGB (`r210`, `R10b`, `R10l`) now upload
  straight to libplacebo with independently tested masks, source precision,
  and byte order. They do not create an intermediate conversion buffer when
  P010 is not selected. P010-only analysis remains deliberately unavailable
  on those direct RGB paths, rather than causing a silent conversion.
- R12L continues to use its existing P010 fallback and R12B remains unsupported
  in Alpha; neither is claimed as lossless/native. Their 36-bit packing needs
  a separately qualified unpack/upload path. A raw-GPU v210 unpack is also
  future work; it requires output validation on the actual D3D11/libplacebo
  devices before replacing the proven P210 route.
- Golden tests prove all captured luma/chroma values for v210 and UYVY P210,
  including v210 padding-edge handling and UYVY vertical-chroma preservation.
  Packed RGB mask tests prove r210/R10b/R10l component placement and endian
  handling. x64 Release build and test suite: **460/460 passed**.

### Beta queue configuration reconciliation (2026-08-02)

- After rebasing on beta commit `7f1ae2f`, removed the remaining stale
  `alpha_queue_size` parsing, state, reference documentation, and public-field
  inventory entry. The key remains deliberately rejected by schema validation.
  Alpha therefore uses only `queue_size` as its hard capacity and
  `[queue] target_frames` as the maintained live target.
- Rebuilt x64 Release and verified the complete suite: **461/461 passed**.
- Deployed the clean x64 Release artifacts from source commit `20acbdd` to
  `C:\\Videoprocessor\\vp` on 2026-08-02. Only `VideoProcessor.exe` and
  `vprenderer\\VideoProcessorVPRenderer.dll` were replaced, each after a
  timestamped backup and SHA-256 match; the active `VideoProcessor.cfg` was
  not changed.

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
