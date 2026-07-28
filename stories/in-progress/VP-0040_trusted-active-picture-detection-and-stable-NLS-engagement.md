# VP-0040: Trusted active-picture detection and stable NLS engagement

## Status

In Progress as of 2026-07-28. The current
`billslack2/videoprocessor` default branch was discovered as
`v1.1.014-beta`, and the developer confirmed commit
`05d7318f87c613dcc54618386076566fa58b6580` as the implementation base.
Readiness review and Phase A implementation are underway in the isolated
`codex/vp-0040-trusted-active-picture` worktree.

## Implementation progress

- 2026-07-28: Confirmed the implementation base and began the readiness
  review.
- 2026-07-28: Initial inspection verified the documented sparse luma-only
  detector, ambiguous-timeout crop promotion, geometry publication boundary,
  and late NLS output-contract restart decision on the approved base.
- Next: establish the pure P010 evidence component and fail-safe crop-authority
  model for Phase A before beginning the separately reviewable Phase B
  transaction changes.

## Expert review record

This story was reviewed from three independent perspectives before being
finalized:

- video/image processing: distinguish real encoded bars from dark textured
  content using spatial, photometric, chroma, and boundary evidence rather
  than a fixed sparse-luma threshold;
- safety/state transitions: separate temporal stability from trusted crop
  authority and never allow repetition alone to promote ambiguous geometry
  into a destructive crop;
- DirectShow/NLS runtime: decide and negotiate the output contract before
  installing NLS so initial engagement has one controlled visual transition.

The reconciled design below treats preservation of source pixels as the
highest-priority failure mode: uncertainty may temporarily retain bars or use
linear/full-raster presentation, but it must not crop valid picture content.

## User story

As a viewer using NLS on both 16:9 and 2.35:1 viewports, I want VP to
distinguish real encoded black bars from dark artwork, menus, fades, credits,
and overlays, so NLS never cuts off valid edges, flashes between false
rectangles, or remains stuck on a bad crop. When I arm NLS, I want at most one
intentional output-contract transition before the final mapping appears.

## Field report and reproducible evidence

The Apple TV home menu provides a deterministic reproduction:

1. Select the 2.35:1 `scope` viewport.
2. Display a bright Apple TV menu background. The correct NLS result preserves
   the complete 3840x2160 source raster.
3. Arm NLS.
4. Allow the menu artwork to transition to a dark background without changing
   layout or source aspect.
5. VP first changes size during NLS engagement, changes again after renderer
   replacement, and can then visibly crop the left and right edges. Depending
   on the dark artwork duration, the wrong crop can remain latched.

The deployed log
`C:\Videoprocessor\vp\logs\vp_debug.20260728-090546.log` proves that the
configured viewport is correct and isolates the failure to source-rectangle
detection:

- `09:04:10`: NLS uses the correct full rectangle `0,0-3840,2160`, source
  aspect `1.7778`, target `2.3500`, and stretch `1.32188`.
- The shader is installed before VP reports
  `renderer_restart=1`; VP then rebuilds for output picture aspect `235:100`.
- `09:04:12`: renderer generation 2 reacquires the same full rectangle and
  installs NLS again. These two installs explain the initial two-size
  engagement.
- `09:04:19`: dark edge content is promoted as stable geometry
  `0,0-3724,2160`, aspect `1.7241`, for reason
  `ambiguous transition reached conservative confidence`. NLS raises stretch
  to `1.36305`.
- `09:04:20`: a second false rectangle is promoted:
  `132,0-3724,2160`, aspect `1.6630`. NLS crops 132 source pixels from the
  left and 116 from the right and raises stretch to `1.41314`.
- No actual source-layout or viewport change occurred. Only the menu artwork
  became dark.

This is not a shader-configuration or `$screen_aspect` ownership problem.
`screen_aspect=47:20` correctly describes the target viewport. The active
picture detector independently supplies the source crop and is granting crop
authority to dark scene content.

## Current-code diagnosis

### Pixel classifier

`CBufferedLiveSourceVideoOutputPin::UpdateActivePictureAspectRatio` currently:

- uses a fixed P010 luma threshold of 96, where limited-range black is 64;
- samples only 64 positions per row and 36 positions per column;
- labels a row or column black when 27/32 of those samples pass;
- walks inward from each raster edge until a sampled row or column is no
  longer classified as black;
- records only the resulting bounds and a coarse opposing-bar symmetry flag.

At 3840 pixels, the current opposing-edge symmetry tolerance is approximately
21 pixels. The field candidate's 132-pixel left edge and 116-pixel right edge
differ by only 16 pixels, so dark artwork can eventually be labeled symmetric
despite having no affirmative bar evidence.

The classifier does not prove that a candidate region is a true bar. It does
not measure:

- a per-frame or per-stream black floor;
- bar luma distribution, variance, texture, or connectedness;
- P010 chroma neutrality and chroma variance;
- a sustained inner boundary between bar and active content;
- agreement across multiple rows/columns inside the candidate band;
- independent confidence for each edge;
- whether a small crop is worth the destructive risk.

A dark or black menu background can therefore satisfy the same sparse luma
test as a genuine encoded pillarbox bar.

### Transition policy

`ActivePictureTransitionModel` separates clear symmetric changes from
ambiguous changes, but both can ultimately become stable:

- clear symmetric candidates use two confirmations;
- ambiguous candidates use five confirmations;
- `CommitCandidate` makes either result the stable published rectangle;
- unavailable/black/fade observations preserve the last stable mapping.

The test
`AmbiguousSustainedTransitionUsesConservativeConfidence` explicitly requires
an ambiguous rectangle to become stable after enough repetitions. The field
failure shows that temporal repetition cannot turn weak spatial evidence into
safe crop evidence. Once the false rectangle is committed, preserving the last
stable mapping also explains the apparently stuck crop.

Contradictions and reversals are currently diagnostic counts rather than veto
evidence, `SameBounds` does not carry evidence/symmetry into candidate
identity, and initial acquisition can trust any repeatable rectangle before a
safe full-raster baseline exists.

### NLS engagement ordering

`DirectShowGenericHDRVideoRenderer::SelectShaderRule` currently evaluates and
installs the NLS rule, then calculates whether the selected output aspect
requires renderer restart. For a 16:9-to-2.35 output-contract change, users can
see:

1. NLS applied to the old renderer contract;
2. renderer teardown/rebuild;
3. NLS Waiting while the new detector epoch initializes;
4. NLS applied again to the new renderer.

The one permitted contract restart is being decided too late in the operation.

## Safety objective

Active-picture detection is a pixel-preservation boundary. A false negative
may temporarily retain black bars. A false positive can discard valid picture
content and feed the wrong aspect into nonlinear geometry. Therefore:

> VP may crop only when it has affirmative, independently reviewable evidence
> of encoded bars. Darkness or temporal persistence alone is not evidence
> sufficient to remove pixels.

## Required invariants

1. The selected viewport describes only the target screen. It never proves a
   source crop.
2. Full-raster presentation is always a safe fallback.
3. Temporal stability and crop trust are separate properties.
4. An `AMBIGUOUS` or `UNAVAILABLE` observation never acquires crop authority
   merely by repeating.
5. Every cropped edge has explicit affirmative evidence. Destructive cropping
   is atomic per orientation: if either opposing edge lacks trusted evidence,
   neither edge in that pair may crop.
6. Opposing bars required for a destructive side or top/bottom crop must be
   geometrically coherent. Asymmetric evidence fails closed unless a future,
   separately validated contract explicitly supports asymmetric mattes.
7. Contradictory evidence may withdraw a crop to full-raster safe
   presentation; it must not replace one trusted crop with an untrusted crop.
8. Each accepted geometry change creates exactly one trusted revision, and
   each consumer applies that revision at most once. One detector epoch may
   publish multiple legitimate Scope/IMAX/content transitions. Candidate
   jitter is diagnostic only.
9. NLS parameters are compiled from one coherent snapshot containing the
   trusted rectangle, crop evidence, source aspect, source media
   signature/detector epoch, and viewport target; installation binds that
   snapshot to the current renderer generation.
10. Renderer-owned work/results from an old generation never reach a
    replacement renderer. A source-owned trusted snapshot may cross a renderer
    replacement only after explicit unchanged-epoch/media-signature
    revalidation.
11. Arming NLS across an output-contract change installs no NLS shader on the
    old renderer. The shader is installed once after the new contract and
    trusted geometry are ready.
12. Confirmed content-aspect transitions under an unchanged output contract
    remain renderer-restart-free.
13. One observation cannot change effective crop geometry, remove NLS, or
    publish a transient Waiting state during ordinary content playback.
14. Aspect-only confidence never authorizes exact crop coordinates. NLS must
    not synthesize `active_left`, `active_top`, `active_right`, or
    `active_bottom` from a scalar aspect estimate.
15. Detector epoch, trusted-geometry revision, observation sequence, and
    renderer generation are separate identities with explicit lifetime rules.
16. Candidate confidence cannot cross a source/scene discontinuity,
    format/raster change, detector reset, or renderer replacement.
17. Contradictions and reversals reset or materially decay candidate
    confidence; they cannot coexist with an unconditional
    `confidence=1.0` commit.
18. Inward/contraction changes require stronger evidence because they discard
    pixels. Expansion toward raster boundaries confirms faster because it is
    pixel-preserving.
19. Full raster is the immediate startup/reset safety baseline derived from
    the media type; it does not need to be earned by image detection.

## Proposed detection contract

Replace the detector's Boolean notion of stable geometry with a typed snapshot
that separates observation, temporal confidence, and crop authority. Exact
names may change during implementation, but the semantics must include:

```text
ActivePictureSnapshot
  raster bounds
  proposed active bounds
  measured source aspect
  classification:
    FULL_RASTER_TRUSTED
    BAR_CROP_TRUSTED
    PROVISIONAL
    UNAVAILABLE
  per-edge evidence:
    candidate width
    black-floor distance
    high-percentile luma
    luma dispersion / texture
    neutral-chroma score
    inner-boundary contrast
    spatial continuity
    opposing-edge symmetry error
    confidence
  temporal confidence and observation counts
  detector epoch
  observation sequence
  trusted-geometry revision
  reason code
```

Only `FULL_RASTER_TRUSTED` and `BAR_CROP_TRUSTED` snapshots can feed crop
coordinates to NLS. `PROVISIONAL` observations remain visible in diagnostics
but cannot remove source pixels.

The evidence snapshot is source-owned and bound to a media signature/raster,
not intrinsically owned by madVR. Renderer generation is checked only when a
consumer applies that snapshot. This permits deliberate revalidation or safe
carry-forward across a renderer replacement when the source epoch and media
signature are unchanged, while still rejecting stale renderer work.

## Robust bar-evidence extraction

Refactor pixel inspection into a deterministic, renderer-neutral function that
can be tested with synthetic P010 planes and recorded/reconstructed frames.
Keep transition policy outside the pixel classifier.

For each orientation and edge:

1. **Estimate black safely.**
   Use robust low-percentile statistics from candidate perimeter regions and
   the legal P010 range rather than treating every value below one fixed code
   as black. Support limited/full-range variation, raised black, small
   compression noise, and SDR/HDR input without allowing an ordinary dark
   scene to redefine black upward.
2. **Measure a band, not isolated lines.**
   Sample a bounded two-dimensional grid over multiple rows and columns across
   the complete candidate bar. Require spatial continuity over the
   perpendicular dimension. One dark column or row cannot define an edge.
3. **Reject texture.**
   Measure high-percentile luma, median absolute deviation or equivalent
   dispersion, bright-pixel occupancy, and gradient/texture energy. True bars
   should be near the measured black floor and low texture. Dark artwork with
   visible structure must fail.
4. **Use chroma evidence.**
   When the P010 chroma plane is available, require candidate bars to be close
   to neutral chroma with low chroma dispersion. Tolerate normal codec noise,
   but reject dark colored artwork that luma alone misclassifies.
5. **Prove the inner boundary.**
   Require a spatially sustained contrast/structure transition between the
   candidate bar and the proposed active picture. A few bright pixels,
   subtitles, logos, UI badges, or edge highlights are insufficient.
6. **Require opposing-edge coherence.**
   Side bars and top/bottom bars must have compatible widths and evidence.
   Geometric tolerance must use scan quantization, raster scaling, and a
   strict calibrated center-offset ceiling. Photometric noise may reduce bar
   confidence but must not widen geometric symmetry tolerance. Symmetry
   corroborates two proven edges; it is not proof that either edge is a bar.
7. **Treat small crops as high risk.**
   Small side crops such as the observed 3% edges require stronger evidence
   than large canonical pillarbox bars. If evidence is inconclusive, preserve
   the full raster. Thresholds must be corpus-derived, documented, and
   internal unless field evidence proves a user control is necessary.
8. **Avoid aspect whitelists as the primary detector.**
   Common 4:3, 16:9, 1.85/1.90, 2.00, 2.20, 2.35/2.39, and IMAX shapes are
   useful test controls and plausibility signals, but VP must not silently
   force every valid production into a short list of ratios.

The implementation may use a fixed-resolution edge grid or integral
statistics to keep cost independent of 4K pixel count. It must not introduce a
full-frame per-frame scan on capture or presentation threads.

An initial engineering envelope is approximately 10,000-30,000 bounded luma
samples plus subsampled UV evidence per 4K analysis, with a coarse scan and
local refinement. Use fixed/preallocated scratch storage. The readiness review
must replace this envelope with measured p99 time, CPU, memory-traffic, and
queue-headroom budgets.

## Transition and crop-authority policy

Use an explicit fail-safe state model:

```text
SAFE_FULL_RASTER / ACQUIRING / DEGRADED
  -> PROBING_BAR_CANDIDATE
  -> TRUSTED_BAR_CROP
  -> PROBING_CHANGE
  -> SAFE_FULL_RASTER or TRUSTED_BAR_CROP
```

Required behavior:

- Full raster is published immediately as the safe initial/reset authority
  because its coordinates come from the media type and discard no pixels.
  Detection may separately record affirmative full-frame picture evidence.
- A proposed crop enters `PROVISIONAL_BAR_CANDIDATE`; it cannot publish crop
  coordinates until spatial bar evidence and temporal confirmation both pass.
- A genuinely clear bar appearance/disappearance may use a short bounded
  confirmation window, but never one observation.
- Ambiguous or asymmetric candidates never time out into a trusted crop.
- Candidate reversals reset candidate confidence without changing the last
  trusted geometry.
- During ordinary ambiguity, hold the last trusted effective mapping and do
  not clear/reinstall the shader. If repeated affirmative evidence proves that
  picture is now visible outside an old crop before a replacement crop is
  trusted, publish one `SAFE_FULL_RASTER` trusted revision and apply
  full-raster-safe NLS parameters atomically. Reserve renderer-acquisition
  Waiting for startup/replacement where no usable trusted source snapshot is
  available.
- Fades and all-black frames are `UNAVAILABLE`, not new geometry. They do not
  manufacture a crop.
- Recovery from provisional/unavailable state is explicit and logged.
- Repeated identical snapshots do not increment the trusted-geometry revision.
- A rate limiter or equivalent coalescing policy prevents multiple shader
  recompilations from edge jitter, but must not mask a genuine trusted
  transition.
- Unavailable/fade evidence alone preserves the last trusted authority; it
  neither creates nor withdraws a crop.
- Every transient state has a documented bounded exit: commit a strong
  candidate, restore the last trusted mapping, or use safe full raster.
  Oscillation cannot leave the consumer indefinitely Waiting or unstable.
- Candidate confirmation resets across every source/scene/format/detector
  epoch boundary.

The existing test that requires ambiguous sustained geometry to become stable
must be replaced. Temporal confidence can confirm a candidate only after its
spatial evidence is independently eligible for crop authority.

## Stable NLS engagement sequence

Split manual NLS selection into contract preparation and shader application:

1. Start a correlated transaction containing:
   - latest requested intent and target viewport;
   - pending output contract;
   - currently effective/installed selection;
   - monotonically increasing request/transaction ID.
2. Calculate the desired output/media aspect without installing a shader.
3. If the output contract differs:
   - set OSD/runtime state to `NLS: Preparing`;
   - request exactly one controlled renderer restart;
   - do not compile or install NLS on the old renderer.
4. Create the replacement renderer with the desired output contract and a new
   renderer generation and a new detector epoch.
5. Revalidate a source-owned trusted snapshot when source epoch/media
   signature are unchanged, or acquire new trusted geometry before exposing
   mapped presentation.
6. Install NLS exactly once from the coherent trusted snapshot and only then
   mark that selection effective.
7. If the output contract already matches, skip restart and apply once from
   current trusted geometry.
8. Later trusted content-geometry changes update shader parameters without a
   renderer restart.

NLS Off and viewport changes retain their existing deliberate contract
semantics, but the same prepare/restart/apply ordering must prevent application
to a renderer already scheduled for replacement.

No replacement-renderer frame may be visibly presented under the new NLS
output contract while its required mapping is absent or based on untrusted
geometry. The implementation may safely revalidate/carry a source-owned
snapshot across an unchanged media epoch, or gate/blank presentation until
acquisition and installation complete; the story requires the visual outcome
without prescribing the mechanism.

Duplicate On requests coalesce into one transaction/restart. On -> Off,
viewport changes, or a newer request during preparation/acquisition converge
to the latest intent. Completion from an obsolete transaction cannot install
a shader, publish Active, or trigger a restart loop.

## Observability

Every candidate diagnostic must make false positives independently
explainable without logging full video frames by default. Log:

- raster, proposed bounds, trusted bounds, and both aspects;
- classification and crop-authority state;
- per-edge bar width and confidence;
- black-floor estimate, luma percentile/dispersion, chroma-neutral score,
  texture score, boundary contrast, and symmetry error;
- spatial and temporal thresholds used;
- candidate matches, contradictions, reversals, and elapsed milliseconds;
- detector epoch, observation sequence, trusted-geometry revision, viewport
  generation, renderer generation, and NLS transaction ID;
- whether NLS preserved, withdrew, or changed crop;
- requested NLS effect, mapping mode, output contract, shader-install count,
  and restart decision;
- a stable machine-searchable reason code plus concise human-readable reason.

OSD should distinguish `NLS: Preparing`, `NLS: Waiting`, active mapping modes,
and Off. Candidate-level detector noise belongs in logs, not rapidly changing
OSD text.

Recommended concise phases are `NLS: Arming (aspect restart)`,
`NLS: Acquiring picture`, `NLS: Active 1.78 -> 2.35`,
`NLS: Holding trusted picture (ambiguous)`, `NLS: Safe full frame`, and
`NLS: Off`. OSD must never say Active unless the shader is installed in the
current renderer generation.

Add an opt-in diagnostic mode capable of retaining bounded anonymized edge
statistics or a small downsampled luma/chroma edge grid for reproducible field
analysis. It must be off by default, size-bounded, and must not retain full
frames unless a separate explicit developer-only control is deliberately
approved.

## Configuration policy

Do not add user-facing black thresholds, confirmation counts, or detector
presets in the first implementation. Calibrate internal constants from the
corpus and document them in code and tests. Configuration is justified only
if repeatable hardware/content evidence proves that one safe policy cannot
cover supported inputs.

## Implementation work breakdown

1. Extract the P010 edge classifier from
   `CBufferedLiveSourceVideoOutputPin` into a pure, bounded component with a
   testable input view and typed evidence output.
2. Add safe P010 luma/chroma sampling with explicit pitch, plane bounds, range,
   and overflow validation.
3. Implement per-edge band statistics, texture/chroma/boundary evidence, and
   opposing-edge coherence.
4. Extend the active-picture snapshot and publication APIs with
   classification, crop authority, reason, and the explicit detector epoch,
   observation sequence, and trusted-geometry revision identities.
5. Replace the transition model's ambiguous-timeout promotion with
   evidence-gated states and safe crop withdrawal/recovery.
6. Remove the aspect-only fallback that fabricates centered crop coordinates;
   update DirectShow consumers so only exact trusted crop bounds or explicit
   safe full-raster bounds reach NLS.
7. Reorder manual NLS selection to prepare output contract before shader
   installation.
8. Coalesce trusted-geometry revision changes so one accepted transition
   causes at most one shader-chain update.
9. Update OSD and structured diagnostics.
10. Build the deterministic corpus, automated tests, performance benchmark,
    and hardware reproduction checklist before tuning final thresholds.

## Delivery phases and separability

The field report contains two independently caused defects and they must remain
separately reviewable/bisectable even if delivered under VP-0040:

- **Phase A - detection and crop authority (primary):** pixel evidence,
  fail-safe state model, trusted revisions, NLS consumer boundary, and the dark
  menu regression. This phase must not be delayed by renderer transaction
  work.
- **Phase B - transactional NLS engagement (adjacent):** preflight output
  contract, cancellation/coalescing, replacement presentation gating, and
  single final shader application.

Use separate implementation commits and validation evidence. If readiness
review finds that Phase B has an independent design unknown, split it into a
linked numbered story without weakening or postponing Phase A acceptance.

## Deterministic verification corpus

The corpus must exercise pixel classification and time sequences, not only
hand-constructed rectangles.

### Must remain full raster

- Apple TV menu with the same layout transitioning between the supplied bright
  and dark artwork;
- full-frame dark films with bright center subjects and dark edges;
- dark colored edge artwork with near-black luma but non-neutral chroma;
- static menus, game HUDs, credits, channel bugs, lower thirds, and UI badges;
- fades to/from black and runs of all-black frames;
- subtitles or graphics occupying/crossing the would-be discarded region when
  no already-verified relocation path has preserved them;
- bright or dark edge flashes lasting one to several analysis intervals;
- textured, noisy, compressed, raised-black, and dithered edge content;
- asymmetric edge darkness and opposing candidates that arrive at different
  times.

### Must produce trusted crops

- clean 4:3 pillarbox in 16:9;
- 1.66 pillarbox control;
- 1.85/1.90 and IMAX mattes;
- 2.00, 2.20, 2.35, and 2.39 letterbox controls;
- bars with small codec noise, raised black, or isolated contamination that
  remains outside the discarded region or has already been preserved by a
  verified subtitle-relocation path;
- repeated Scope/IMAX cuts in both directions;
- bar appearance/disappearance through a short fade.

### Sequence controls

- start NLS on both bright and dark menu artwork;
- arm NLS before and after selecting 2.35 viewport;
- repeated NLS On/Off;
- unrelated renderer replacement while NLS is armed;
- viewport 16:9 -> 2.35 -> 16:9;
- detector epoch replacement with old geometry arriving late;
- source/scene epoch change while a candidate is accumulating;
- 23.976/24, 25, 29.97/30, 50, and 59.94/60 fps.

Synthetic generators must preserve exact P010 code values and provide
ground-truth bar bounds. The supplied screenshots are post-render evidence,
not detector-input fixtures. Before threshold acceptance, retain a durable,
sanitized P010 detector-input capture or bounded edge-grid/statistics trace
from the failure. Keep a reconstructed synthetic menu fixture as a companion,
not a substitute, so the defect cannot regress behind overly simple
solid-color tests.

## Automated verification

1. Pixel tests prove correct luma/chroma plane bounds, pitch handling, black
   floor estimation, percentile/dispersion calculations, and edge evidence.
2. Bright-to-dark Apple TV-style frames never publish a crop or change NLS
   parameters.
3. Ambiguous sustained dark edges remain provisional regardless of duration.
4. Asymmetric side candidates cannot crop either side.
5. A trusted crop contradicted by new visible edge content withdraws safely.
   Either add a cheap every-frame sentinel plus pre-presentation invalidation,
   or measure and enforce a maximum stale-crop frame/latency bound from the
   first analyzed strong contradiction. Do not claim an impossible zero-frame
   guarantee from an approximately 80 ms sampled detector.
6. True bars with bounded noise and overlay contamination still confirm within
   documented latency.
7. Candidate jitter produces no trusted-revision or shader-install
   churn.
8. Renderer-generation tests reject stale geometry.
9. Runtime tests prove restart decision precedes shader installation.
10. A 16:9-to-2.35 manual arm performs one renderer restart, zero installs on
    the old renderer, and one install on the replacement renderer.
11. Trusted content-aspect changes under one output contract perform zero
    renderer restarts.
12. OSD/log tests distinguish Preparing/Acquiring, Holding trusted geometry
    during ambiguity, Safe full frame, Active, and Off, and cover safe
    withdrawal and recovery.
13. The full existing detector, NLS, viewport, queue, HDR, subtitle, and
    renderer-switch test suites remain green.
14. Property/fuzz tests prove that any sequence lacking strong crop evidence
    can never contract effective bounds; every inward trusted revision has a
    current-epoch proof; transient states are bounded; and trusted revisions
    are monotonic.
15. The recorded false-candidate sequence
    `0-3764, 0-3724, 0-3740, 0-3724, 132-3724` cannot change safe
    full-raster geometry.
16. Startup on dark artwork begins at safe full raster and cannot establish a
    crop from repetition alone.
17. Aspect-only evidence cannot populate NLS crop coordinates.
18. Duplicate On, On -> Off, and viewport changes during both pre-teardown and
    acquisition phases converge to latest intent with no obsolete install,
    restart loop, or duplicate restart.
19. A frame/video-capture integration test proves the replacement renderer
    never exposes the new NLS output contract without its required trusted
    mapping.

## Performance and concurrency requirements

- Edge analysis remains bounded independently of raster pixel count.
- Measure p50/p95/p99 analysis time at 1920x1080 and 3840x2160 on the
  supported conversion worker; record CPU use and memory traffic.
- Establish an explicit p99 time/CPU/memory budget before implementation,
  expressed relative to the 60 fps frame interval and measured conversion
  worker/queue headroom, not merely the approximately 80-100 ms analysis
  cadence. It must not materially change conversion/delivery queue depth.
- No filesystem, renderer COM, shader compilation, or UI work runs on the
  detector's conversion-worker path.
- Publish immutable snapshots with existing epoch/revision/lifetime
  protections.
- Renderer polling/notification consumes snapshots without holding the
  detector mutex during shader compilation.
- No detector state change may reset capture, conversion, delivery, madVR,
  HDR, or timing queues.

## Hardware validation

On the deployed Apple TV/DeckLink/madVR/projector path:

1. Repeat the bright-to-dark menu transition at least 50 times while NLS is
   armed. Require zero trusted crop publications, zero clipped frames after
   the intended output transition, and zero NLS parameter changes caused only
   by artwork darkness.
2. Arm NLS at least 20 times from each output-contract state. Require one
   visible transition when a contract restart is necessary and none when it is
   already satisfied. Verify from frame/video capture, not only restart and
   shader-install counters.
3. Play the genuine Scope/IMAX/4:3 corpus and confirm bars are removed only
   after trusted evidence.
4. Exercise subtitles, credits, fades, overlays, and dark scenes for an
   extended control run.
5. Confirm no unexpected renderer restart, graph reconnect, queue reset,
   dropped-frame burst, HDR transition, or persistent Waiting state.
6. Preserve logs showing detector evidence, trusted publications, shader
   installs, renderer generations, and restart counts.

Cover the real 3840x2160/59.94 P010 madVR path in windowed and
exclusive/fullscreen modes where supported; HDMI YUV and RGB conversion paths;
SDR, HDR10, and LLDV/DV state changes; queue-only reset, HDMI resync/dropout,
resolution/rate transition, and unrelated renderer rebuild.

## Acceptance criteria

- The supplied dark Apple TV menu transition never changes the trusted
  full-raster rectangle and never clips either horizontal edge.
- No ambiguous/asymmetric observation can acquire crop authority through
  elapsed time or repetition alone.
- Every NLS crop is traceable to affirmative per-edge spatial evidence plus
  temporal confirmation.
- A false or contradicted crop fails safely toward full-raster presentation,
  never toward removing additional source pixels.
- The detector does not remain stuck on a false crop after affirmative
  contradictory picture evidence. Unavailable/fade evidence alone preserves
  the last genuinely trusted authority and cannot create a crop.
- Real 4:3, Scope, IMAX, and supported intermediate bars still confirm within
  a documented bounded latency at all frame-rate families.
- Arming NLS across a required output-contract change performs exactly one
  controlled restart and installs NLS only on the replacement renderer.
- No replacement-renderer frame is visibly presented under a new NLS output
  contract while required mapping is missing or untrusted.
- Arming NLS when the output contract already matches performs no restart and
  one shader installation.
- Confirmed content-aspect changes under one viewport/output contract remain
  restart-free.
- Candidate noise causes no visible flashing, OSD oscillation, publication
  churn, or repeated shader compilation.
- Pending transactions coalesce/cancel to latest user intent without obsolete
  shader application, duplicate restart, or restart loop.
- Detector analysis stays within the accepted 4K performance budget and does
  not degrade queue or presentation health.
- Logs and tests are sufficient for an independent reviewer to reconstruct
  why every crop was accepted, rejected, withdrawn, or restored.

## Dependencies and boundaries

- Refines the field-tested behavior delivered by VP-0035; it does not merely
  increase that story's confirmation count.
- Depends on VP-0034 durable/restart-free NLS runtime state and VP-0038's
  application-owned viewport target.
- Does not change the simplified shader configuration schema.
- Does not infer source crop from a viewport/profile name.
- Does not query or configure madVR's physical screen profile.
- Does not add computer-vision object recognition or menu-specific
  application detection.
- Does not require perfect recovery of asymmetric or artistically embedded
  mattes; uncertain cases preserve pixels.
- Pixel analysis cannot always distinguish a perfectly uniform, neutral,
  centered black UI frame from mathematically identical encoded bars. In that
  irreducibly ambiguous case the mandated result is no new crop.
- The missing optional `Debanding mild.hlsl` observed in the same log is a
  separate deployment/configuration issue and is not a cause of this defect.

## Readiness review before implementation

Before moving to In Progress, confirm:

- the source branch base through the mandatory default-branch/developer gate;
- whether chroma plane access and pitch are reliable for every supported P010
  sample path;
- the exact immutable snapshot/API boundary shared by detector and NLS;
- the deterministic corpus storage format and any privacy constraints;
- the measured baseline detector cost and the proposed 4K budget;
- that no unknown in DirectShow media-type negotiation invalidates the
  prepare/restart/apply sequence;
- an independent reviewer agrees that the chosen thresholds satisfy the
  pixel-preservation invariants rather than only the supplied reproduction.
