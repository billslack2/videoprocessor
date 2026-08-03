# VP-0080: Make Alpha active-picture cropping fail safe on live full-raster video

## Status

Backlog (critical picture-integrity defect, 2026-08-02). The deployed Alpha
renderer repeatedly cropped and enlarged ordinary 16:9 World Cup pictures after
mistaking scene content for encoded letterbox bars. The required experts are
named below, but implementation cannot begin until their direct engineering and
test participation is established and the pre-coding review is completed.
Alpha scope playback should be treated as unsafe for important live viewing
until a fail-safe increment is validated and deployed.

## Pre-development readiness record (2026-08-02)

### Gate state

Implementation is not yet authorized. The required participants,
qualifications, and decision rule are recorded below, but the owner clarified
that Bill and Urvish must work with the engineers on the implementation, review
it, and write/review its unit tests. Their collaboration channel and concrete
pre-coding design/test contributions are not yet recorded. A clean worktree and
local branch, `codex/vp-0080-alpha-crop-failsafe`, remain code-clean until that
gate is satisfied.

The operating context was re-established before that preparation:

- `billslack2/videoprocessor` is the repository of record;
- GitHub reported `v1.1.015-beta` as the current default integration branch;
- the isolated branch starts at integration commit `8b8d900`; and
- the authoritative source checkout and deployed checkout both contain
  unrelated work, so neither was modified.

The untouched integration commit has a clean x64 Release baseline: the full
solution built with zero warnings/errors and all 487 native tests passed. This
is an environment/readiness result, not validation of a VP-0080 fix.

### Participant and decision record

- **Bill — video-renderer geometry participant.** Bill has 20 years of relevant
  experience, including six years working on MPC. His role is to review the
  source-crop, viewport, subtitle, NLS, and final-output geometry contracts and
  keep the solution practical and efficient for this live-viewing use case.
- **Urvish — image/signal-analysis participant.** Urvish is a Carnegie Mellon
  University graduate with 30 years of relevant experience and is contributing
  this review in support of the community. His role is to review black-bar,
  boundary, visible-content, scene, temporal, format, and corpus evidence.

Both Bill and Urvish must agree before implementation proceeds. Design choices
should prefer a practical, efficient solution for the actual VP use case over
unnecessary algorithmic complexity. Bill's production experience is the final
guide on implementation practicality, but it cannot waive the unanimous gate
or the source-pixel-preservation invariant; an unresolved critical objection
from either participant still blocks coding.

The participant qualification portion of the gate is satisfied. The
pre-development and implementation-participation portions remain pending. Bill
and Urvish must directly contribute to or review the proposed state/authority
policy and test design before coding, remain engaged while the code and unit
tests are written, and both approve the resulting implementation. Their actual
decisions, test contributions, and unresolved objections must be recorded; a
generic owner authorization is not a substitute for that evidence.

### Confirmed failure model

The incident transitions in `C:\Videoprocessor\vp\logs\vp_debug.log` were
re-read and match the rectangles and release times recorded above. Inspection
of the current integration source confirms one bypass around the VP-0040
authority model:

1. `vprenderer/LibplaceboVideoRenderer.cpp::UpdateScopeSubtitleShift` derives
   its black floor from bottom-edge samples in only the outer fifths of the
   frame. Its proposed bar rows are also evaluated only in those outer fifths.
2. Approximate top/bottom symmetry, an active aspect in `1.90..3.00`, and eight
   repeated candidates promote `scopeSubtitlePictureTop/Bottom`. There is no
   affirmative whole-edge boundary/interior proof and no scene/epoch-owned
   authority snapshot in this path.
3. `configureScreenProfile` then writes those renderer-local values directly
   to `source.crop` and marks them trusted. This path is separate from the
   shared `P010ActivePictureEvidence` plus `ActivePictureTransitionModel`
   contract already used by Alpha NLS.
4. `subtitle_fit: false` is not an automatic-crop off switch. The local
   geometry is still analyzed and promoted before the function checks the
   subtitle-fit setting, and the promoted rectangle can still change
   `source.crop`.
5. With NLS waiting, Alpha explicitly preserves full raster. With NLS mapped,
   it consumes the stronger shared evidence. The unsafe bypass is reached for
   ordinary Scope profile rendering when NLS is not providing mapped trusted
   geometry.

This explains the World Cup failure without threshold speculation: repeated
dark scene structure was allowed to become destructive authority in a consumer
that should never have owned that authority.

### Proposed first fail-safe increment for expert review

The recommended first increment is deliberately smaller than a detector
rewrite:

1. Make Alpha automatic source cropping explicitly configurable and default it
   to **Off**. Off must keep `source.crop` at the complete generation-current
   input raster for every analyzer result. Scope viewport fit/pillarbox may
   remain active, but analyzer-driven zoom, NLS crop, and subtitle-derived
   source cropping must not.
2. Remove crop authority from the renderer-local scope/subtitle detector. It
   may remain temporarily as non-authoritative diagnostic or displacement
   evidence, but it cannot set a trusted flag, populate `source.crop`, or
   preserve/deepen a crop.
3. When automatic crop is later enabled, permit only a current-epoch snapshot
   from the shared crop-authority state machine to change `source.crop`.
   Subtitle fit, viewport selection, NLS state, target aspect, and temporal
   repetition remain consumers or supporting observations, never authorities.
4. Keep raw candidate, temporal state, trusted authority, subtitle request,
   source crop, and final output rectangle as separately logged facts. The
   first increment must at minimum log the off/default decision and any
   rejected crop request with consumer and reason.
5. Document the switch in `CONFIGURATION.html` and the generated/public field
   inventory. The OSD/log must make `Automatic crop: Off (full raster)`
   distinguishable from unavailable analysis and from an explicit manual
   geometry command.

This phase intentionally accepts visible encoded bars. Its safety claim is
limited and measurable: while automatic crop is Off, no observation sequence,
format path, viewport change, subtitle state, NLS state, or renderer epoch may
contract the source rectangle. It does not claim that the eventual automatic
detector is ready.

### Proposed proof package for the pre-coding review

Before any threshold work, the experts should accept or amend the following:

- **False-positive release bound:** zero automatic source-crop transitions and
  zero source-pixel loss over the full-raster corpus. Phase A enforces this by
  policy with automatic cropping Off by default.
- **Acquisition bound:** not applicable to Phase A because encoded bars remain
  visible. A numeric acquisition bound for a later enabled detector must be
  approved against the genuine letterbox corpus before tuning.
- **Withdrawal bound:** disabling automatic crop, changing renderer/source
  epoch, or losing generation validity restores full raster before the next
  presented frame. A later enabled detector needs a separately approved bound
  from first strong contradiction to full-raster presentation.
- **Incident control:** add deterministic 3840x2160 full-raster sequences for
  the six observed false candidates (`258/264`, `274/276`, `116/106`,
  `272/274`, `272/274`, and `230/236` top/bottom bar depths), including stable
  repetition, scene cuts, score graphics, captions, crowd/grass bands, and
  camera motion. No raw World Cup frame is currently tracked, so a
  rights-safe representative sports capture is still required.
- **Genuine-bar controls:** encoded 1.85, 2.00, 2.20, 2.35, 2.39, and 2.40
  material, with noise, raised blacks, logos, and subtitles in bars. Phase A
  expects full raster; Phase B expected rectangles and acquisition/withdrawal
  bounds require expert approval.
- **Deterministic policy/property tests:** automatic-crop Off is invariant;
  provisional or subtitle-only geometry cannot contract; stale generations
  cannot contract; only every-edge trusted authority may contract when the
  future On path is exercised; contradiction and reset can only expand toward
  full raster.
- **Format and runtime matrix:** P010, P210, and native RGB; SDR/HDR and valid
  full/limited ranges; raster, viewport, renderer, channel, format, scene, and
  epoch transitions; focused tests, the full native suite, and a clean x64
  Release build before any live validation.

The mature-implementation comparison supports this split: MPC Video Renderer
keeps original size, crop rectangle, final video rectangle, and cropped-output
rectangle distinct; MPC-HC exposes zoom/pan as an explicit user action; and
FFmpeg separates black from motion/edge modes, supports outliers, and can
retain the largest observed area. These are design constraints and diagnostic
comparators, not license to copy third-party code or treat another detector as
the crop oracle.

## Incident evidence

The current deployed log at
`C:\Videoprocessor\vp\logs\vp_debug.log` records multiple false active-picture
transitions during fixed-format 3840x2160 P210 World Cup playback on 2026-08-02:

| Time | Incorrect Alpha decision | Observed recovery |
| --- | --- | --- |
| 20:50:10 | aspect `2.3443`, crop `0,258-3840,1896` | full raster at 20:50:12 |
| 20:50:25 | aspect `2.3851`, crop `0,274-3840,1884` | full raster at 20:50:44 |
| 20:51:40 | aspect `1.9814`, crop `0,116-3840,2054` | full raster at 20:51:42 |
| 20:54:31 | aspect `2.3792`, crop `0,272-3840,1886` | full raster at 20:54:40 |
| 20:54:57 | aspect `2.3792`, crop `0,272-3840,1886` | full raster at 20:55:00 |
| 20:55:01 | aspect `2.2668`, crop `0,230-3840,1924` | full raster at 20:55:32 |

Each accepted rectangle was applied as `source.crop` by
`LibplaceboVideoRenderer.cpp`, so the event removed valid top and bottom picture
pixels and enlarged the surviving image. This was not an output-mode, window,
brightness, NLS-shader, or native-RGB-ingress failure. The input was P210 and
the erroneous geometry came from Alpha's renderer-local scope/subtitle detector.

The detector samples primarily the outer fifths of each scan line, derives a
relative black level from bottom-edge samples, accepts a broad `1.90..3.00`
aspect range, and grants crop authority after eight matching analyses. Repeated
low-luma scene structure can therefore acquire destructive authority without
proving that complete encoded bars exist. The World Cup incident demonstrates
that temporal repetition and approximate symmetry are not sufficient evidence.

## User story

As a viewer of live sports and other full-raster programming on a CIH/scope
screen, I need Alpha never to crop or enlarge the picture merely because scene
content resembles black bars, so no action, scoreboard, captions, or other
valid pixels disappear during playback.

## Severity and safety invariant

This is a picture-integrity defect and a release blocker for automatic Alpha
cropping. A false negative may leave encoded bars visible; a false positive
destroys valid picture content. The required bias is therefore asymmetric:

> Uncertain geometry is full raster. Temporal stability, a plausible aspect,
> low luma, or approximate symmetry can support a candidate, but none can grant
> crop authority without affirmative spatial evidence for every cropped edge.

Any contradictory visible-picture evidence must withdraw a crop toward full
raster; it must never deepen, retain, or replace a crop on ambiguity alone.

## Established practice to incorporate

The design and review must compare behavior, state ownership, and failure modes
with established implementations; do not copy third-party code without a
separate license and provenance review.

1. **MPC/MPC Video Renderer geometry contract.** MPC's subtitle/rendering
   interface represents `videoCropRect`, `originalVideoSize`,
   `videoOutputRect`, and `croppedVideoOutputRect` as distinct facts. VP must
   likewise keep observed source geometry, trusted crop authority, and final
   viewport mapping separate instead of allowing subtitle evidence to mutate
   the source crop implicitly. Reference:
   <https://github.com/Aleksoid1978/VideoRenderer/blob/master/Include/SubRenderIntf.h>.
2. **MPC-HC explicit zoom/pan behavior.** MPC-HC exposes pan-and-scan/zoom as
   explicit user-controlled geometry. Automatic analysis must not become an
   unreported equivalent of a zoom command. Reference:
   <https://github.com/clsid2/mpc-hc/blob/develop/src/mpc-hc/MainFrm.cpp>.
3. **FFmpeg crop-detection practice.** FFmpeg separates black-threshold
   detection from motion-plus-edge detection, scans for the video area, has an
   explicit outlier concept, and can preserve the largest observed area across
   playback. VP need not adopt its algorithm, but must account for these mature
   safeguards when choosing evidence, outlier, and history policies. References:
   <https://ffmpeg.org/ffmpeg-filters.html#cropdetect> and
   <https://github.com/FFmpeg/FFmpeg/blob/master/libavfilter/vf_cropdetect.c>.
4. **Existing VP crop-authority contract.** VP-0040 and VP-0062 already require
   affirmative opposing-edge evidence, distinguish provisional geometry from
   authority, prohibit promotion by repetition alone, and fail safely to full
   raster. Alpha may not maintain a weaker renderer-local authority path.

## Mandatory expert participation and review gates

Expert involvement is part of the work, not an optional consultation.

1. Before implementation starts, record in this story and the source PR:
   - a participant with demonstrated MPC-HC, MPC Video Renderer, madVR, or
     comparable production video-renderer geometry experience; and
   - a participant with demonstrated video/image signal-analysis experience in
     black-bar, active-picture, edge, motion, or temporal-confidence detection.
2. At least one of those experts must work with the implementer before coding
   to review the incident trace, failure model, crop-authority state machine,
   proposed corpus, and measurable false-positive/release bounds. Record the
   decisions and unresolved objections. A link-only or after-the-fact review
   does not satisfy this gate.
3. At least one expert who did not author the implementation must independently
   review the final algorithm, tests, diagnostics, and live evidence. The
   reviewer must explicitly address destructive-crop safety, sports content,
   subtitles/overlays, scene transitions, and genuine letterbox acquisition.
4. The story cannot move to Review until the pre-development participation is
   evidenced. It cannot move to Done, merge, or deploy until the independent
   expert review is recorded and all critical objections are resolved or
   explicitly accepted by the owner.
5. Automated tests and AI review may supplement these roles but cannot be the
   sole evidence for either expert gate.

If suitable participants are unavailable, keep the story in Backlog or Blocked
and leave automatic Alpha crop disabled/fail-safe; do not weaken this gate to
ship the detector.

## Required design

1. Establish one authoritative active-picture/crop snapshot shared by Alpha
   NLS, scope viewport mapping, subtitle fit, OSD placement, and diagnostics.
   Remove or constrain any renderer-local path that can independently grant
   destructive crop authority.
2. Separate these states and generations explicitly:
   - raw observation/candidate rectangle;
   - temporal confidence;
   - per-edge affirmative evidence;
   - trusted crop authority or trusted full-raster authority;
   - subtitle/overlay evidence and requested displacement; and
   - final source crop, viewport fit, and output rectangle.
3. Only trusted crop authority may populate `source.crop`. Subtitle detection,
   caption holds, a configured scope profile, target screen aspect, or NLS
   state must not create or enlarge a source crop.
4. Require coherent evidence across enough of each complete proposed bar and
   its boundary. Sampling must cover the frame spatially, including central
   regions that the current outer-fifths test ignores. Define and validate
   treatment of compression noise, raised blacks, HDR/SDR range, chromatic dark
   pixels, gradients, vignettes, grass/crowd bands, score graphics, captions,
   logos, replay wipes, and camera motion.
5. Acquisition must be conservative and scene-aware. Repetition of the same
   candidate cannot manufacture authority. A candidate spanning a scene cut,
   source reset, format/raster change, viewport generation, or renderer epoch
   must restart its proof.
6. Withdrawal must favor visible content. Affirmative pixels outside the
   trusted rectangle revoke the affected crop promptly within a bound agreed
   during expert design review. Ambiguous/dark frames may retain the last truly
   trusted crop only for a measured, bounded interval; they cannot establish or
   deepen it.
7. Track the largest affirmatively visible source area within the applicable
   scene/epoch and use it as contradiction evidence, taking the lesson from
   mature crop detectors without allowing stale history across genuine aspect
   changes.
8. Preserve coordinate and chroma alignment for P010, P210, and native RGB
   analysis. Crop bounds must remain valid through rotation, pixel aspect,
   interlace/deinterlace, raster, and viewport transforms.
9. The first safe increment may disable Alpha automatic source cropping while
   retaining non-destructive fit/pillarbox behavior. Do not delay that fail-safe
   behind a more ambitious detector rewrite if automatic cropping cannot yet be
   proven safe.
10. Provide a documented user-visible off switch for automatic crop. Off must
    mean no analyzer can alter the source rectangle, while manual/explicit
    geometry remains distinguishable in logs and OSD.

## Diagnostics

On state changes, log a concise, reconstructable record containing:

- frame/source sequence, scene/epoch, format, range, raster, and viewport;
- observed and trusted rectangles plus generation;
- per-edge blackness, boundary, interior-visible-content, symmetry, motion,
  spatial-coverage, and temporal evidence summaries;
- candidate, provisional, trusted-crop, trusted-full-raster, hold, withdrawal,
  and rejection states with exact reason;
- the consumer that requested geometry and whether `source.crop` changed; and
- before/after source crop and final output rectangle.

Normal logs must report transitions rather than every frame. A bounded
diagnostic mode may capture per-analysis evidence for corpus work.

## Verification corpus and method

The expert participants must approve the corpus and expected decisions before
threshold tuning. Preserve incident-relevant samples where rights and storage
permit; otherwise capture a reproducible representative sports corpus.

1. Run at least one continuous regulation-length 59.94/60 fps sports control,
   including wide pitch views, crowd/stand shots, tunnels, studio inserts,
   scoreboards, captions, replay wipes, camera pans, dark uniforms, night
   scenes, and commercial transitions. Require zero automatic crops and zero
   source-pixel loss on full-raster material.
2. Include genuine encoded 1.85, 2.00, 2.20, 2.35, 2.39, and 2.40 letterbox
   controls, with and without subtitles/logos in the bars. Require correct,
   bounded acquisition without oscillation.
3. Include dark full-raster films, fades, credits, title cards, animation,
   menus, high-black artwork, vignettes, near-symmetric scenery, static shots,
   news/studio lower thirds, and live broadcast graphics. Require no crop from
   luma, symmetry, or repetition alone.
4. Include mixed-aspect transitions and real full-raster/letterbox changes.
   Measure acquisition, contradiction, withdrawal, and stale-authority latency
   in frames and milliseconds.
5. Exercise P010, P210, and supported native RGB paths in SDR and HDR, limited
   and full range where valid, plus format/raster, refresh, renderer, viewport,
   channel, and epoch transitions.
6. Add deterministic unit/reference-frame tests, transition-sequence tests,
   and property/fuzz tests proving that no observation sequence lacking
   affirmative per-edge evidence can produce a destructive crop.
7. Compare VP candidate rectangles against FFmpeg `cropdetect` black and
   `mvedges` output on the reference corpus as diagnostic evidence. Agreement
   is not an oracle; every VP crop must still satisfy VP's stronger live-viewing
   safety invariant.
8. Measure analysis CPU/GPU cost, allocations, queue depth, presentation
   latency, and dropped/repeated frames. Safety fixes must not introduce an
   unbounded worker, readback stall, or full-resolution per-frame copy.

## Acceptance criteria

- The captured World Cup pattern and the approved long-form sports corpus
  produce zero automatic crop transitions and zero lost valid pixels.
- Only a generation-current trusted crop snapshot with affirmative evidence
  for every cropped edge can change `source.crop`.
- Provisional, ambiguous, dark, asymmetric, subtitle-only, profile-derived, or
  temporally repeated evidence cannot acquire or deepen crop authority.
- Contradictory visible content withdraws toward full raster within the
  expert-approved bound, without a zoom flash, oscillation, stale crop, shader
  rebuild, renderer restart, or queue discontinuity.
- Genuine encoded letterbox controls acquire and retain the correct crop within
  the approved bound, and subtitles/overlays do not become crop evidence.
- Automatic-crop off is complete, observable, documented, and preserves valid
  non-destructive viewport behavior.
- Logs make every accepted, rejected, retained, and withdrawn crop independently
  explainable from bounded evidence.
- Focused and full x64 Release tests pass, followed by live Alpha validation on
  the deployment display.
- The recorded pre-development and independent-review expert gates are both
  satisfied before merge/deployment.

## Dependencies and related work

- VP-0040: trusted active-picture detection and stable NLS engagement.
- VP-0062: safe full-raster fallback for ambiguous/high-black content.
- VP-0035: robust active-aspect transitions.
- VP-0044: Alpha OSD placement from final visible-picture geometry.
- VP-0075: native RGB analysis parity; this defect is format-independent and
  was observed on P210, but the repaired authority contract must cover both.
- VP-0070: later subtitle/glyph relocation; subtitle evidence must remain
  non-authoritative for source cropping.
