# VP-0075: Restore Alpha analysis parity on native RGB ingress

## Status

Done (2026-08-02). Merged by PR #31 to `v1.1.015-beta` as `8b8d900` after a
successful x64 Release build and complete 487/487 test run. Direct hardware and
live-playback validation was unavailable; the user explicitly accepted the
automated Release evidence as the completion gate. No binaries were deployed.
This story covers the existing `subtitle_fit` picture-shift behavior only;
broader VP-0070 OCR/glyph relocation remains out of scope.

## Progress evidence

- 2026-08-02: Added the first bounded, non-owning analysis-input contract on
  source branch `codex/vp-0075-native-rgb-analysis` at `0f41173`. It samples
  P010 or direct native RGB in source coordinates and does not create a
  full-frame P010 buffer or GPU readback.
- Native Alpha NLS active-picture evidence and scene detection now consume
  the contract. Direct DeckLink R210 is the primary test format; BGRA, R10b,
  and R10l have layout coverage. Scope `subtitle_fit` follows in the next
  milestone below; advanced subtitle OCR/relocation is not part of VP-0075.
- Focused x64 Release verification passed: 21 P010 active-picture, scene, and
  native-RGB tests; library, VPRenderer DLL, and GUI builds all succeeded.
  No deployed binaries were changed. Live DeckLink R210 and reference-corpus
  playback/latency measurements are still required before parity is claimed.
- 2026-08-02: Rebased onto the current `v1.1.015-beta` integration branch and
  added `79f86c5`, which feeds the existing scope subtitle/bar evidence from
  the native source sampler and removes its recurring median-buffer heap
  allocation. The unit corpus now covers R210 scope bars, pillarbox, dark
  artwork, chromatic-dark rejection, and generation-safe scene input (23
  focused tests). This is still validation-gated; no native build is deployed.
- 2026-08-02: Expanded capture-format coverage to DeckLink R12B and R12L.
  R12L was already converted to P010; R12B now uses the same proven P010
  fallback, including exact SMPTE 268M packed-12-bit decoding. The regular
  32-bit RGB layouts (ARGB/BGRA/R210/R10b/R10l) remain direct-upload plus
  sparse native analysis. R12B/R12L cannot safely use that generic uploader
  because eight pixels occupy a 36-byte packed block. Focused x64 Release
  tests passed, including 4K60 smoke conversion (R12B ~6.9 ms average,
  R12L ~7.6 ms average on the development machine). No deployed binaries
  were changed.
- 2026-08-02: Made the shared P010/P210 analysis representation explicit so
  4:2:2 capture formats retain their full-height chroma rows rather than being
  sampled like 4:2:0. Focused x64 Release active-picture, scene, native-RGB,
  and P210-plane tests passed (25 total); no deployed binaries were changed.
- 2026-08-02: Rebased onto integrated R12B and padded-v210 support, then made
  MadVR automatically deliver both R12B and R12L through the proven P010
  contract (`8bc062d`). R210 remains RGB48 by default and MPC's independent
  R12B RGB48 policy is unchanged. Optimized the exact SMPTE 268M R12B decoder
  byte mapping (`8dd3076`) after the first integrated benchmark left too
  little 60 Hz headroom. The optimized 4K run measured about 9.9 ms average
  and 13.6 ms maximum for R12B-to-P010; three consecutive 4K smoke runs and
  the complete x64 Release suite passed (487/487). Library, VPRenderer DLL,
  and GUI builds succeeded. No deployed binaries were changed.
- 2026-08-02: Re-fetched the current GitHub default integration branch, verified
  the feature branch was 0 commits behind, rebuilt the complete x64 Release
  solution with 0 errors, and reran all 487 tests successfully. PR #31 was then
  merged to `v1.1.015-beta` as `8b8d900`. GitHub had no required PR checks.
  Direct hardware/live testing was not performed and was explicitly waived by
  the user for story completion. No deployment was performed.

## User story

As an Alpha-renderer user with a direct/native RGB input path, I want NLS,
active-picture/black-bar detection, scene analysis, and scope subtitle-fit
analysis to remain available and trustworthy, so using native RGB upload does
not trade away the picture-aware behavior available on P010/P210 paths.

## Current behavior

VP-0069-1 intentionally introduced direct libplacebo RGB upload for supported
regular ARGB/BGRA/R210/R10b/R10l input. R12B/R12L use a reliable P010 fallback
because their SMPTE-packed 12-bit layout cannot be represented by the generic
four-byte RGB uploader. Before VP-0075, the direct regular-RGB path marked
P010-oriented NLS, scene, and scope-subtitle analysis unavailable. This branch
now supplies bounded native analysis for those direct formats without a
full-frame conversion. The automated Release corpus is the accepted completion
evidence; live hardware playback remains useful follow-up evidence, not a gate.

The goal is **analysis parity without restoring unconditional full-frame P010
conversion**. This is not a visual renderer change: direct RGB upload remains
the picture-delivery path.

## Scope

1. Define a small renderer-independent analysis-input contract that provides
   the actual information consumers require—principally bounded luminance and
   contrast evidence with correct coordinates—rather than requiring a P010
   `VideoFrame`.
2. Supply that contract for accepted native RGB ingress formats using one
   bounded method selected by evidence, such as source-native CPU luma
   extraction or a small downsampled GPU/CPU analysis surface.
3. Migrate these consumers to the contract:
   - active-picture/black-bar detection and trusted active rectangle;
   - NLS aspect evidence and safe state transitions;
   - scene-detection input required by the Alpha path; and
   - existing scope `subtitle_fit` bar-content analysis used to shift the
     picture enough to keep captions visible.
4. Preserve coordinate equivalence from source raster through crop, viewport,
   NLS, and final output. Consumers must not confuse rendered black, picture
   black, CIH bars, UI, or letterbox/pillarbox geometry.
5. Keep truthful availability reporting. Until a consumer has passed its
   native-RGB validation, OSD/logging must continue to say unavailable rather
   than claiming parity.

## Non-goals

- Do not route native RGB rendering through P010 merely to satisfy analysis.
- Do not add OCR, advanced subtitle relocation, or panel restoration; VP-0070
  owns those later actions. Existing `subtitle_fit` picture shifting remains
  in scope.
- Do not alter image color, tone mapping, LUT processing, output range,
  renderer queues, frame offset, or presentation scheduling.
- Do not add per-frame GPU readback, a staging-resource stall, an unbounded
  worker queue, or a second full-resolution frame copy.

## Required design constraints

- The analysis representation may be lower resolution and lower precision only
  where validation proves it does not weaken active-rectangle/glyph decisions.
- Range and transfer handling must be explicit. For RGB sources, derive
  luminance in a way that remains meaningful across full/limited-range input,
  SDR/HDR transfer, and Rec.709/BT.2020 primaries. This is detection evidence,
  not a replacement color pipeline.
- Analysis must carry its sampling-to-source transform, dimensions, generation,
  and validity/confidence. A stale native-RGB analysis result must never be
  applied after a format, raster, viewport, renderer, or epoch transition.
- If an input format lacks a safe bounded extractor, retain native rendering
  and report the dependent feature unavailable; choose P010 only when an
  explicit user/configuration path requests that known fallback.
- Reuse the existing trusted-evidence rejection policy for fades, dark scenes,
  app UI, credits, and high-black artwork. Do not lower confidence thresholds
  merely to make native RGB appear supported.

## Required investigation and implementation sequence

1. Inventory each current P010-only consumer and document the exact samples,
   precision, spatial scale, and temporal stability it needs.
2. Prototype the smallest luma/contrast extractor for one native RGB format;
   measure CPU cost, allocations, GPU synchronization, and queue/present impact
   against both direct-RGB-with-analysis-unavailable and P010 reference paths.
3. Establish synthetic and recorded reference corpus results from the existing
   P010/P210 implementation before enabling native RGB decisions.
4. Add active-picture/NLS parity first. Enable it only after trusted-rectangle
   equivalence and false-positive rejection are demonstrated.
5. Add scene-analysis parity next, with epoch/generation invalidation tests.
6. Feed the existing scope `subtitle_fit` bar-content analysis used for picture
   shifting without beginning VP-0070 OCR or advanced relocation behavior.
7. Enable each capability independently with a clearly logged availability
   state; do not require all consumers to ship atomically.

## Diagnostics

At source/renderer generation and availability changes, log:

- ingress format and color metadata;
- analysis representation, dimensions, sampling transform, and execution path;
- enabled/unavailable consumer capabilities and exact reason;
- evidence confidence/validity and generation match or rejection reason; and
- bounded timing/allocation summary per test/session.

Do not log every frame or expose raw per-frame subtitle/scene results in normal
debug logs.

## Verification

1. Compare native-RGB and P010/P210 reference active rectangles and confidence
   for 16:9, scope, 4:3, letterboxed, pillarboxed, mixed-aspect, dark, fade,
   credit, app-UI, and high-black-artwork samples.
2. Validate NLS engagement/release and aspect transitions without a renderer
   restart, queue backlog, false stretch, or delayed stale decision.
3. Validate scene analysis through renderer switch, format/raster change,
   display refresh change, HDR/SDR transition, reset, and epoch replacement.
4. Validate the existing scope `subtitle_fit` bar-content evidence used to
   shift the picture. OCR, glyph capture, and advanced relocation remain
   VP-0070 work and are not acceptance gates for this story.
5. Measure 23.976 and 59.94/60 native RGB playback in SDR and HDR where the
   source path supports it. No measurable new steady-state queue, dropped-frame,
   presentation-latency, or cold-path stall regression is permitted.
6. Confirm explicit P010 selection preserves the existing fully supported
   analysis behavior as a comparison/fallback path.

## Acceptance criteria

- Native RGB Alpha ingress retains direct picture upload and gains trusted
  active-picture/NLS analysis without full-frame P010 conversion.
- Scene and existing `subtitle_fit` bar-content consumers receive a
  generation-safe, coordinate-correct analysis contract, or remain clearly
  unavailable with a precise reason.
- Native RGB behavior matches P010/P210 reference decisions on the defined
  corpus without increasing false positives in dark/UI/fade content.
- No hidden readback, full-frame copy, queue, renderer rebuild, or latency
  regression is introduced.
- OSD and logs accurately distinguish available native analysis, explicit P010
  fallback, and unsupported/unavailable capability states.

## Dependencies and related work

- VP-0069-1 (Done): native Alpha ingress and conditional P010 baseline.
- VP-0023: P010 range/metadata contract, used as color/reference evidence.
- VP-0040 and VP-0035: trusted active-picture and robust NLS behavior.
- VP-0070: later glyph/subtitle capture and relocation work.
- `src\\VideoProcessor-Lib\\vprenderer\\LibplaceboVideoRenderer.cpp` and the
  current active-picture/NLS/scene analysis implementation.
