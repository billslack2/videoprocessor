# Analysis recovery architecture and selective rollback

Status: historical recovery record with subsequent changes below. See the 2026-10-14 section for the current analysis-source contract and validation.
Date: 2026-10-14

The original compatibility phase below deliberately preserved different live and
queued sampling choices. The user's 2026-10-14 consistency request supersedes
that temporary selection policy, as documented at the end. Detector admission,
crop safety, and temporal evidence requirements are separate contracts.

## Decision and authoritative baseline

The user's latest direction supersedes using 1.3 as the implementation baseline:
keep the remote 1.4 beta's multiline, subtitle-card and script fixes; exclude the
later analysis unification and follow-up experiments. Preserve DV support as a
separate port, not as a reason to import unrelated changes.

Repository: billslack2/videoprocessor. GitHub default and latest beta were checked
and fetched on this date: `v1.4.01-beta`,
`089d32dfe07033a17fce84d0a0b306f2a0b0ae59`.

Recovery branch: `codex/analysis-behavior-recovery-20261013`.
It starts from that exact remote beta, rather than reverting a dirty checkout.
This preserves `59eacc2a` complete-card/multiline recovery, `0b89b7a7` oversized glyph recovery, and `d3d40434` card/donor regression fixes already merged in the remote beta.

The initial documentation commit changed no runtime behavior. The recovery implementation now follows this contract; active settings remain untouched.

The old experimental branch is preserved at
`codex/analysis-frame-unification-20261013`, committed tip
`27876f3d10d18710e145b62b5ea4c421b64ca530`, with its uncommitted changes intact.
A copy of that working diff is also preserved outside the repository:
`E:/codex/diagnostics/analysis-regression-20261013/preserved-experimental-working.patch`.

Historical 1.3.005 beta `554b9f9871b25b5992ddd280ba8d9e47d66d1fdc` remains a
behavioral reference for AR and edge restrictions. It lacks the current subtitle
box detector, so it is not the source rollback target.

## What the audit established

- The native-first selector in `2eb5bc34` changed input selection, not just API
  shape. It selects native YUV/RGB even where live analysis previously consumed
  the formatter's P010/P210 output. Chroma filtering and rounding can change
  evidence while all detector thresholds remain identical.
- The beta already has different source selection in live processing and queued
  subtitle/lookahead processing. A single helper does not prove equivalence.
- Existing `AnalysisLumaSource` exposes full-resolution source coordinates and
  10-bit luma/chroma evidence. Native RGB provides per-pixel chroma. P210 groups
  horizontal chroma; P010 also groups vertical chroma. A forced conversion is
  therefore not universally behavior preserving.
- DV packed RGB transport is not RGB picture evidence. Decoded 12-bit 4:2:2
  storage and its validated metadata must be used. Existing detector sampling
  takes the upper ten bits; retain this explicit compatibility behavior first.
- The experimental selector records depth/range on some paths without making
  generic sampling honor them. It also derives converted layout from rendering
  policy. The replacement contract must describe the actual analysis payload.
- Recent attempts also changed subtitle admission, temporal confirmation and
  retention. Those are policy changes, not prerequisites for a format adapter.
- Current-bar checking is a separate evidence-policy issue. The shared retained
  AR geometry path predates the latest adapter; do not attribute every observed
  regression to one commit without reproducing it.

## Analysis inventory

| Consumer | Current code / role | Recovery requirement |
| --- | --- | --- |
| Live active-picture detection and automatic AR/NLS | `vprenderer/LibplaceboVideoRenderer.cpp`, `ActivePictureEvidence`, `ActivePictureAnalyzer` | Preserve beta source sampling, crop bounds, qualification and release behavior. |
| Buffered AR lookahead, expansion and crop scheduling | `AnalyzeActivePictureLookahead`, `BufferedPictureExpansion`, `ActivePictureDecisionTimeline` | Preserve frame ordering and exact effective-frame decisions; no future votes applied to the wrong displayed frame. |
| Local boundary refinement, near-black and remembered-edge evidence | `LocalBoundaryRefinement`, `RememberedEdgeReturn`, renderer diagnostics | Same source coordinates and interpretation as their frame; retained geometry is distinct from current observations. |
| Subtitle discovery, script grouping and card boundaries | `SubtitleBoxDetector`, `SubtitleBoxLookahead`, `SubtitleBarEvidence` | Preserve remote beta language, multiline and card behavior; no threshold or search-area expansion during format work. |
| Subtitle lookahead, worker refresh, hold and replacement | `SubtitleMeasurementWorker`, lookahead/presentation state | Own frame storage through completion; retain beta timing and identity rules; measure onset, expiry and replacements. |
| Scene detection / near-black / safe correction points | `SceneDetector`, `FrameProcessor::AnalyzeScene`, `CBufferedLiveSourceVideoOutputPin::AnalyzeSceneDetector`, VP renderer | Preserve sampled values, histograms and event timing. The separate DirectShow consumer must be inventoried and tested, not silently migrated. |
| HDR peak/tone-map analysis | `HdrPeakAnalysisCrop` and libplacebo renderer | Actual peak analysis stays with full-quality render pixels. It may consume proven crop geometry; a reduced analysis proxy must not drive color or luminance statistics. |
| Subtitle glyph capture, background reconstruction and composition | subtitle GPU capture/repair shaders | These process displayed pixels, not merely detection samples. Retain render-resolution input; map analysis coordinates exactly. |
| Legacy DirectShow GPU/OCR subtitle path | `CBufferedLiveSourceVideoOutputPin`, `GpuSubtitleDetector`, `WindowsOcrSubtitleDetector` | Preserve backend behavior; migration requires its own parity gate. |

## Architecture

```text
Captured frame + immutable transport identity + per-frame metadata
                  |
           validated decode/unpack (DV where required)
                  |
          +-------+--------------------------------+
          |                                        |
Render payload                              Analysis payload / view
native/high-precision upload                 owned immutable storage
DV metadata retained                        explicit sample contract
          |                                        |
GPU peak analysis / tone map                 compatibility samplers
subtitle capture and composition             AR / subtitles / scene evidence
          |                                        |
       display <------- frame-keyed decisions ------+
```

Rendering and analysis may share immutable storage when their actual layouts
match. Neither is forced to consume the other's reduced or reformatted output.
Analysis must not read post-tone-map, scaled, cropped or subtitle-composited
pixels. P010 remains supported for rendering and existing analysis inputs.

### Logical contract before one physical format

The common interface must specify:

- Native raster dimensions and source-coordinate mapping (initially identity).
- Plane addresses, strides, layout, chroma subsampling, siting and reconstruction.
- Significant bits and alignment; coded range including any retained headroom;
  matrix/primaries and transfer interpretation; exact rounding and clipping.
- The compatibility sampling version that yields detector evidence, separately
  from storage precision. Include this sampling profile in the interpretation/cache key: baseline live and queued consumers can use different chroma footprints for the same frame. Do not globally renormalize range while keeping old
  thresholds and assume the behavior is unchanged.
- Per-frame DV decoding validity and interpretation, including metadata changes
  that alter sample meaning. Missing DV metadata is not permission to treat a
  tunnel as SDR. Declared SDR fallback needs its own verified adapter.
- Immutable source/transport identity, interpretation key and backing-store
  owner. Format changes invalidate incompatible evidence, not transport history
  by changing the accepted sequence or inventing a new frame.

First implement compatibility adapters for the beta's existing selection paths.
All consumers can use one interface without immediately forcing the same storage
or silently reconciling differences in what they currently sample. Reconcile
those differences one consumer at a time through differential evidence.

A full-resolution P210-style analysis plane is a candidate optimization for
4:2:2 input and decoded DV. For DV, call the layout 16-bit 4:2:2 with 12 significant
bits rather than mislabelling it standard 10-bit P210. Reuse decoded storage when
possible. P010 input cannot regain lost chroma detail by expanding into P210.
Native RGB's per-pixel chroma must survive compatibility testing: retain the
native sampler or use 4:4:4 analysis storage if 4:2:2 changes decisions. Do not
claim lossy chroma conversion, downsampling or bit reduction is lossless.

### Ownership, scheduling and caching

Constructing an owned native view must not wait for renderer conversion. Only consumers needing decoded/formatted storage await that payload.

Use the existing asynchronous conversion/decode machinery. Publish a ready,
immutable analysis object with its storage owner and actual layout; never expose
a borrowed view whose lifetime depends on an unrelated caller capture. Cache
preparation once per source frame and interpretation, and reuse it for live,
lookahead and subtitle worker consumers. The captured frame remains available
for consumers that need baseline native semantics.

Avoid full-frame preparation or shader compilation on display/UI threads. Keep
queues and storage bounded. A packed 16-bit 4:2:2 3840x2160 surface is 33,177,600
bytes before alignment, so allocation per lookahead consumer is unacceptable.
Measure preparation, sampling, retained memory, queue age and render cadence.
Rendering must not wait for optional shadow comparisons. If analysis is pending
or invalid, apply the existing bounded unavailable/hold policy; do not turn
pending work into positive subtitle or crop evidence. A bounded hold may reuse evidence only within the same validated analysis domain; DV-to-SDR/unknown transitions must invalidate incompatible AR, subtitle and scene evidence.

## Selective recovery manifest

| Change | Disposition |
| --- | --- |
| Remote beta `089d32df` including script, complete-card, donor and multiline fixes | Keep as the implementation and behavior baseline. |
| `f8e39eae` DV tunnel support; `cba1c574` unpack optimization | Port DV-specific functionality separately; validate scalar/vector parity. |
| `c7ba2347`, `11f99a0a`, `b1221383` DV metadata/continuity/luminance | Keep as separately reviewed DV changes; preserve invalid-metadata protection. |
| `d33c1f8d` native RGB subtitle interpretation for DV | Do not port as an independent design. Superseded by decoded-pixel handling. |
| Decoded-pixel fixes in `195003e8`, `9ff6919b` | Port required DV-only hunks after DV support, keeping non-DV sampling unchanged. Manually port `195` because its removal of the skipped `d33` block will not apply as an ordinary cherry-pick. |
| `b55b76e0` directional cleanup shader and settings | Exclude. Independent presentation experiment, with reported shimmer. |
| `2eb5bc34` native-first common source migration | Exclude from recovery. Rebuild the interface from the explicit compatibility contract. Review useful identity protections independently. |
| `472e996e` crowded-top onset gate | Exclude from initial recovery; separate policy proposal only if baseline reproduction establishes a need. |
| `27876f3d` small-edge pending retention | Exclude from initial recovery; do not weaken baseline context identity through the format migration. |
| Dirty shared-bar, fixed-pixel, anchor, retention and source-row changes | Preserve for investigation; do not import collectively or label them proven recovery fixes. |
| New adapter tests | Review and reuse fixtures where meaningful, but assertions preferring a native domain are implementation assumptions, not behavioral requirements. |

`9ff6919b` is an investigation checkpoint, not a release candidate. It includes
the unwanted directional-fill experiment and lacks a complete safe SDR tunnel
fallback analysis contract. Do not simply deploy it as a rollback.

## Ordered implementation and acceptance gates

The first recovery candidate is the remote beta plus the narrow DV port (steps 1-3). Steps 4-6 are subsequent architecture work, not prerequisites for recovery. Apply the release/playback validation in step 7 to either candidate before deployment.

1. **Freeze the reference (prepared here).** Pin remote beta, preserve the
   experimental branch/diff, keep the remote multiline/card implementation.
   Record configurations and baseline outputs without copying example settings
   onto the deployment.
2. **Establish baseline replay.** Run existing remote-beta tests unchanged and
   replay representative non-DV sequences with their actual format, geometry,
   cadence and configuration. Record output decisions, not just a final bool.
3. **Port DV separately.** Introduce only the allowlisted decode/metadata work and
   required DV analysis adapters. Non-DV replay must match beta. DV is checked
   against validated decoded reference pixels and expected geometry, since the
   beta alone is not a DV oracle. Prove transitions into/out of DV and metadata
   loss; do not use raw tunnel bytes as picture evidence.
4. **Introduce the owned logical contract.** Wrap existing source selection and
   unchanged samplers. Run old/new in separate shadow detector and timeline
   states on identical accepted sequences; the new path does not drive display.
5. **Migrate consumers individually.** Scene analysis, live AR, buffered AR and
   subtitle worker/presentation each get explicit parity gates. Reconcile live
   vs queued sampling differences deliberately; no simultaneous threshold edits.
6. **Evaluate common storage.** Compare P210-style preparation against the
   compatibility reference, including RGB chroma edges and P010 averaging.
   Accept only with behavioral parity and measured cost. Precision/resolution
   reduction is a later, separately gated optimization.
7. **Validate presentation and deploy when requested.** Full x64 Release build,
   existing tests, representative sequence replay and visual playback. Verify
   renderer/host pairing and configuration preservation. No claim of recovered
   quality from synthetic test totals alone.

### Required comparison coverage

- UYVY, HDYC, V210 and supported packed RGB, native/formatted overrides, P010 and
  P210 paths; full/limited range, supported matrices, actual strides and packing.
- Near-neutral chroma thresholds, alternating chroma rows/columns, saturated
  edges, anti-aliased glyphs and near-black boundaries, not only grayscale boxes.
- Top/bottom, short CJK, Thai marks, Arabic dots/connected strokes, large Latin
  brackets, one/two/three lines, complete opaque cards and source-area cleanup.
- Mid-picture graphics, scrolling text, changing background, scene cuts, dark
  scenes, retained crop during expansion, rapid cue replacement and pause/resume.
- 23.976 cadence, delayed/out-of-order worker completion, seeks, format switches,
  conversion overrides, DV metadata loss and valid explicit SDR fallback.
- Compare crop bounds/classification and effective frame, line/glyph/card bounds,
  subtitle onset/replacement/expiry and false-positive duration. Compare render
  pixels or capture masks where detection-equivalent results can still erase or
  leave content incorrectly.

Existing expected behavioral results must not be changed to make a migration
pass. If a baseline test is demonstrably wrong, document the independent defect
and review that change separately. API-version bookkeeping may follow an actual
ABI change, with an explicit reason; it is not detector-policy evidence.

## Review and evidence status

Principal-engineer, independent-architecture and subtitle-review agents examined
the boundary. They agree on the remote-beta baseline, separate DV port and an
explicit logical contract before forcing P210 storage.

The previous experimental tree passed 2,417 tests before three more comparison
tests were added. Those later tests compiled but did not link because of an
incremental object/PDB error; they are unverified. Their neutral-chroma synthetic
fixtures do not prove chromatic-edge, Thai/Arabic, scrolling-text or footage
parity. None of those results certifies this recovery design or a deployment.

The initial preparation is followed by the implementation described below. No installation, push or merge has been performed.

## Implemented recovery boundary

The narrow DV decode/metadata port is committed separately through `3361eb00`.
`PreparedAnalysisFrame` now supplies one owned analysis interface to the VP
renderer live analysis, queued AR lookahead and subtitle worker. Rendering keeps
its original native or high-precision converted payload. Decoded DV retains its
12-bit values for rendering; detector evidence uses the established upper-ten-bit
sample convention without rewriting the render surface.

The beta's live and queued source-selection profiles remain explicit. In
particular, queued native 4:2:2 evidence is not silently replaced by P010's
vertically averaged chroma. Existing RGB conversion, range and matrix arithmetic
remain the compatibility reference. Full physical unification is not claimed:
matching interpretations share storage; distinct beta profiles remain distinct.

The new interpretation key covers dimensions, packing, range, transfer,
strides and DV/SDR alignment. Evidence cannot cross incompatible domains, while
accepted capture sequence and transport identity remain unchanged. Invalid DV
metadata produces unavailable analysis unless the formatter explicitly proves a
recognized SDR tunnel alignment. Cache and worker owners retain source storage.

The selective identity protections are reviewed separately from the excluded
native-first migration. Subtitle admission, edge distance, script grouping,
retention, multiline/card geometry and reconstruction policy retain remote beta
behavior. The only pre-existing test assertion changed is renderer API version
24 to 26, corresponding to the actual DV plugin ABI additions.

Separate DirectShow/OCR consumers remain on their established paths. HDR peak
analysis and subtitle GPU capture/composition continue to use render pixels.
### Measured implementation choice

An initial tiled sample-cache prototype matched the captured 4K three-line
frame but took 25.27 ms versus 15.54 ms for direct subtitle scanning, and 1.35 ms
versus 0.68 ms for sparse active-picture extraction in that diagnostic run.
It was removed rather than shipped. The production interface retains immutable
backing ownership and reads existing planar/native pixels directly, without an
extra per-pixel mutex, allocation, full-frame copy or forced chroma conversion.
Decoded DV uses the same direct planar subtitle-sampling path as P210, retaining
its full-height chroma and established upper-ten-bit evidence arithmetic.

The optional captured-frame diagnostic uses `VP_ANALYSIS_P210_FIXTURE`; source
control contains no captured video pixels. An unset variable logs that the
optional diagnostic did not run. Validation here explicitly sets the variable.
The fixture is one paused frame, so it does not certify live playback cadence,
subtitle onset/expiry, scrolling rejection, or all script combinations.

### Recovery validation recorded 2026-10-13

- The complete x64 Release solution build succeeded with the installed Magewell
  SDK. Nothing was deployed.
- The final existing and added C++ suite ran 2,403 tests: 2,402 passed and one
  existing configuration-cache timestamp test failed. All recovery, DV and
  subtitle tests passed in that run.
- `ConfigurationReadCacheTests::SameSizeEditWithRestoredWriteTimeInvalidatesCache`
  returned the prior value after a same-size rewrite with the write timestamp
  restored. Its implementation and tested cache code are unchanged from beta.
  An immediate isolated rerun of all five configuration-cache tests passed.
  The suspected timing dependency is an unchanged filesystem ChangeTime; this
  is a recorded unresolved baseline test/cache limitation, not a green full run.
  Its expectations were not relaxed.
- On the explicitly supplied 3840x2160 decoded P210 fixture, the final suite's
  three-run fresh-detector medians were 13.89 ms direct and 13.89 ms through the
  prepared view. Sparse AR extraction was 0.35/0.30 ms. View preparation rounded
  to 0.00 ms at two decimals. Both detected the same three lines and bounds.
  This compares paths within the recovery binary, not separate beta/recovery
  builds, and excludes upstream DV unpacking and rendering.


### Separate beta-build performance comparison

A separate clean detached worktree at beta `089d32df` was compiled x64 Release.
The same temporary diagnostic harness was compiled against that baseline and
this recovery implementation; production beta source was unchanged. Both read
identical `decoded.p210` bytes at 3840x2160 with picture rows 276..1884.

Each process measured seven fresh detectors and seven reused-detector scans
following a warmup. Source generation stayed constant; sequences advanced and
observations reached eight, preventing duplicate-frame result-cache timings.
Three process runs per build produced these medians (milliseconds):

| Build | Fresh detector | Reused detector |
| --- | ---: | ---: |
| Beta process 1 | 13.573 | 11.419 |
| Beta process 2 | 13.200 | 11.475 |
| Beta process 3 | 13.928 | 11.701 |
| Recovery process 1 | 13.632 | 11.733 |
| Recovery process 2 | 15.633 | 13.441 |
| Recovery process 3 | 13.738 | 11.771 |
| Beta median of process medians | 13.573 | 11.475 |
| Recovery median of process medians | 13.738 | 11.771 |

The observed recovery increase was 0.165 ms fresh (1.2%) and 0.296 ms reused
(2.6%). The experiment does not distinguish small code-generation differences
from system variability. It does establish that the roughly 14 ms fresh scan
cost existed in the beta baseline. Both builds found all three lines with exact
bounds `(1230,1522,2606,1934)`.

This is a detector-only comparison on one frame and one P210 interpretation,
not an end-to-end DV or 60 fps qualification. The 60 fps interval is 16.67 ms;
other work and peaks still need headroom even with an independent worker.
Upstream DV unpacking is excluded, and the zero-copy prepared view is not a
second full-frame conversion. No scan cadence, configuration, thresholds or
subtitle policy were changed to improve these numbers. The common harness and
repeatable report are retained outside source control under
`E:\codex\diagnostics\analysis-baseline-comparison-20261013`.


### Paused-frame acquisition and status follow-up (2026-10-13)

The captured 3840x2160 DV frame demonstrated distinct failures:

- Healthy Alpha occupancy at its hard capacity (4 frames) repeatedly triggered
  queue resets. Depth alternated between 3 and 4 with successful dequeues; no
  overflow or sustained lack of progress was established. Occupancy alone now
  cannot request recovery. Actual overflow still does. DirectShow's separate
  liveness recovery is unchanged; this change does not add an Alpha stall detector.
- A valid DV frame cleared the published luminance status before recomputing it,
  allowing UI polling to show Waiting between valid snapshots. Valid metadata
  now replaces the previous snapshot atomically; invalid/non-DV frames and queue
  generations clear it. Old-generation frames cannot publish into the new one.
- Raw AR sampling stopped on subtitle strokes at rows 272..1902. Independent
  current subtitle flank evidence found rows 273..1884 and detected both lines
  at (1414,1670)-(2414,1914). Mirroring the first raw edge failed the existing
  opposite-edge contrast check. The required shared-picture worker contract
  then prevented subtitle acquisition from a cold start.
- With picture authority available, the initial 72-pixel subtitle translation
  would discard visible picture rows at the opposite edge. The existing crop
  admission veto correctly rejected it, but no caption-bounded fit was offered.

The new startup route uses independently measured flank edges only as a
nomination. It requires the existing strict native AR edge checks, symmetry
limits, horizontal authority and connected-picture veto, plus a detector-owned
crossing caption anchor. It runs under the existing scope gates and feeds the ordinary shared
geometry/transition process. A bounded startup burst supplies the consecutive
observations needed for publication and worker handoff. Thereafter an exact
current worker observation may reaffirm only the established native rectangle,
with a fresh strict inspection of its physical edges. Stale, held, tracked,
pending or cheap-refresh observations cannot supply this authority. No general
AR threshold was relaxed.

An initial translated crop may instead acquire the complete current picture
plus confirmed caption envelope. Exact frame identity and an unmasked current
visible-pixel witness remain mandatory. Unrelated visible pixels veto that fit.
Existing crop admission and near-black episode presentation policies remain
in force. Fresh strict caption-backed bar proof can acquire logical geometry
while a startup episode retains full-frame presentation. This exception applies
only to the two logical geometry acquisition/publication gates; global darkness
and the ordinary presentation release and pixel-containment vetoes still apply.

This is not a general subtitle-mask pass over all raw AR measurements. Known
subtitle exclusion already exists in narrower presentation-retention paths;
expanding it to acquisition and steady-state AR needs explicit current-pixel
ownership and independent edge proof. A rectangular cleanup region must not
silently erase real scene evidence. Renderer source pixels remain unchanged.

Diagnostic video pixels are retained only outside the repository under
`E:\codex\diagnostics\tatooine-paused-20261013`. The opt-in native capture
replay uses `VP_NATIVE_ANALYSIS_FIXTURE` and its existing `.txt` sidecar.


Validation of this follow-up:

- Clean x64 Release solution rebuild succeeded. Incremental link state on this
  machine had produced LNK1103; validation used a clean rebuild instead.
- The complete Release test assembly passed 2,420/2,420. New negative cases keep
  connected picture content rejected even when a previous asymmetric nomination
  is rechecked against changed current pixels. Existing expectations were not
  relaxed. Result: `TestResults/analysis-recovery-subtitle-review-final.trx`.
- The optional native replay recovered picture rows 272..1884 and both subtitle
  lines. One startup proof measured 13.22 ms. The additional strict AR recheck
  using an already available current worker observation measured 0.05 ms; this
  excludes the worker's normal subtitle scan and is not total analysis latency.
- Deployed all five Release binaries at 2026-10-13 15:17:46 local. Backups:
  `C:\Videoprocessor\vp\backups\paused-caption-recovery-20261013-151746`.
  Active configuration was byte-identical before/after deployment.
- Live 23.976 Hz replay acquired strict logical bars at sequence 35, switched to
  current worker reproof by sequence 40, and exited the startup full-raster
  episode through the existing physical-bar/confirmed-translation release.
  At sequence 135 crop admission accepted the bounded 272..1916 first fit;
  normal subtitle translation then reached 72 source pixels. Both subtitle
  lines remained detected during the initial paused interval at
  (1414,1670)-(2414,1914), with no held results. Published final layout filled
  the configured 2560-pixel screen width. The source resumed changing around
  15:18:51; at 15:19:35 the queue still had generation 1, healthy depth 3/4,
  and 120 dequeues per five seconds. No queue recovery/reset had occurred,
  and valid changing DV metadata continued to be reported.

These observations cover the supplied paused scene. They do not establish
visual quality for every scene, sustained 60/120 Hz performance, or every
subtitle language/profile. No renderer image conversion or subtitle glyph
classification thresholds were changed in this follow-up.


### Multi-aspect-ratio city-light false positives (2026-10-13)

The next paused scene exposed two independent failures. Runtime diagnostics
retained the earlier 276..1884 aperture while current native evidence measured
68..2092. Shared subtitle measurement granted the retained planes bar authority
without checking their current pixels, so building lights inside the expanded
picture were admitted as top-bar text. The pending-worker fallback had the same
assumption. Restarting the unchanged binary on the same paused scene acquired
66..2092 and reported no subtitle, confirming the importance of transition
history. The native capture is diagnostic-only, outside source control.

Subtitle analysis now proves current top and bottom bar planes independently.
Fresh physical edges take precedence over retained presentation candidates;
a missing side may reuse a candidate only after current pixel revalidation.
Only current detector-owned crossing ink can explain an obscured edge. The
presentation snapshot remains unchanged: subtitle analysis cannot grant crop
authority. Pending reuse additionally requires unchanged effective current
analysis planes. One unproved side does not suppress a valid opposite side.

Separately, the current outward AR proof rejected the lower picture strip:
p90=36 and texture=7.1 in the failing log, below its absolute 112/12 gates.
Decoded DV has a zero black floor; real dark picture signals can therefore be
well below the absolute luma gate. A separate native-bar expansion proof is
requires current strict boundaries, distributed source-relative
picture signal across every horizontal quartile, and three consecutive distinct
frame identities. Its evidence is independent
of subtitle acceptance, so neither subsystem needs the other to validate an
obsolete aperture. Existing broad-picture and clipping safeguards remain.

Regression coverage includes the native frame with the retained old
aperture, true subtitles at the new edges during pending presentation, single
proved edges, multiline and script profiles, stale pending cues, and dark
picture versus localized bright marks. Legacy shared-authority and worker fixtures
used an almost entirely uniform black raster with no actual picture edge;
those assumptions cannot authorize a historical boundary. Their intended
positive caption/pending scenarios now include physical bands, with separate
negative coverage for unsupported historical boundaries.

Validation: the clean x64 Release solution rebuild succeeded. The complete
2,430-test run passed 2,428 tests; its two failures were legacy worker fixtures
claiming physical bands on a uniform black raster. After fixing those fixtures,
the rebuilt affected suite passed all 599 tests, including both prior failures.
The city-light native replay with retained 276..1884 geometry measured current
67..2091 bars, rejected subtitles, and confirmed independent native/distributed
AR expansion after three fresh sequences. This replay reproduced stale geometry,
not the exact earlier false glyph classification; synthetic coverage exercises
that admission failure. The earlier Tatooine capture also passed separately,
retaining both subtitle lines at (1414,1670)-(2414,1914).
Deployed the five matching x64 Release artifacts at 16:20 on 2026-10-13.
Backup: C:\Videoprocessor\vp\backups\city-lights-multi-ar-20261013-162034.
All deployed hashes matched their build artifacts; active configuration stayed
byte-identical. Startup logs confirmed the new current-independent-subtitle-bars
path and active capture. Playback had advanced to other captions, so the exact
history-dependent city-light transition is established by replay/regression
coverage, not a post-deployment visual observation of that same transition.
### Dark multi-AR acquisition follow-up (2026-10-13)

The 16:21:38–16:21:43 deployed log identifies a second transition problem:
current native bars were 68..2092, old geometry was 276..1884, current global
P90 was 169–171 (not globally near-black), and independent outward evidence was
confirmed 3/3. Nevertheless an earlier FULL_RASTER near-black episode continued
to suppress acquisition. Its saved-entry recovery waits for the old crop to
become safe, which cannot resolve a genuine expansion beyond that crop.
The correction must release obsolete episode ownership using exact-current
independent native expansion proof, then reopen normal acquisition without
publishing a new crop directly or depending on subtitle acceptance.

A separate later interval, after renderer restart, remained genuinely classified
near-black by the current global test (P90=64). That diagnosis requires a new
native capture before changing dark-scene classification. The running player
subsequently switched to a separate vp-patch-match-test installation, so these
sessions must not be conflated. The original log was preserved outside source
control under E:\codex\diagnostics\dark-ar-followup-20261013.
Implemented a separate episode-local confirmation run: three consecutive,
current non-dark native samples, also bound to the independently confirmed
outward proof and exact source/viewport context. Exact certified presentation
repeats preserve progress without voting; bounded detector-step variation stays
anchored to the producer reference. Raw native bounds must equal the selected
candidate so inferred candidates cannot borrow native classification. Success
withdraws obsolete geometry through the existing reset pathway, keeps the
release frame full-raster, and reopens ordinary acquisition. No subtitle vote
or global brightness threshold is changed.

Validation: clean x64 Release renderer/library and test builds succeeded;
671/671 focused tests passed, covering near-black/title guards, AR policy,
subtitle workers, analysis formats, dim side composition, sparse-entry recovery,
and the prior city-light native replay. Independent review drove fixes for
proof history, cadence repeats, detector jitter, and exact native provenance.
This follow-up is not deployed; the separately running test installation is
untouched. The current dark paused-frame classification remains unverified
pending identification/recapture of the reported session.

### Dark paused picture: native capture and AR-only darkness evidence (2026-10-13)

The actual main-instance paused frame was captured from a temporary diagnostic
runtime, then the unchanged installed instance was restored. The installed
configuration and binaries were not replaced. The native capture lives only in
`E:\codex\diagnostics\dark-ar-followup-20261013\paused-1649`; video pixels
are not repository fixtures.

The captured decoded DV frame has code-0 bars and a broad, structured pier/water
picture. Raw native detection supplies `274..1884` on a 3840x2160 raster. The
existing 16x16 global classifier reports P90=80, below its unchanged 96 threshold.
This was blocking acquisition despite correct physical bar detection. Capture
cadence is about 23.976 Hz and display cadence about 59.95 Hz; extra presentations
of the same accepted source frame are not additional evidence.

`EvaluateActivePictureArDarkness` preserves the raw global result and separately
checks current native trusted top/bottom bars against distributed interior
picture evidence. It reads at most 64 bar samples and 576 interior samples only
when the raw frame is globally dark. The exception requires matching native bar
geometry, coherent bar floors, interior median at least 32 codes above that floor,
textured support in at least 12 of 16 tiles across all four quadrants, and at least
16 codes of variation between tile medians. Flat backgrounds, local text, sparse
stars and uniform noise cannot qualify just by raising a global percentile.

The result removes only the AR darkness veto. Live acquisition, retained geometry,
AR lookahead and buffered expansion use the same effective verdict. Ordinary crop
confirmation, current excluded-band pixel safety and episode recovery still run.
Raw darkness remains unchanged for sparse/caption inference and diagnostics.
Established subtitle scanning resumes through independently proved bar authority;
subtitle candidates do not supply the distributed-picture certificate.

Live exceptions are bound to source generation, accepted sequence and analysis
color-domain key. A display repeat can reuse its exact source identity; a new
unmeasured source frame or changed domain cannot inherit the exception. Existing
per-frame retention inspection remains active while a crop is displayed.

The exact paused capture replay finds 15 textured tiles, four quadrants, interior
median55 and tile-median spread25. It keeps raw near-black=true while effective AR
near-black=false, produces zero subtitle detections, and passes startup episode
recovery with two presentation repeats inserted between every fresh source frame.
Repeated presentations add no confirmation votes. This replay validates source
pixels and policy; it is not a claim that the installed player has been updated.


A subsequent isolated live run against the user-paused scene verified the runtime
wiring as well: at 17:24:51 the episode ended, then native picture authority
`0,274..3840,1884` was admitted. The configured viewport applied its existing
small horizontal fill (`28..3810`); the native detector did not invent side bars.
At source sequence120 the diagnostic reported raw P90=86, raw near-black=true,
effective AR near-black=false, 16 textured tiles, four quadrants and no detected
subtitle. After a renderer/source-generation restart, the same native rectangle
was reacquired by sequence125 and remained valid at sequence240. Full evidence:
`E:\codex\diagnostics\dark-ar-followup-20261013\structured-runtime-verification.log`.
The isolated runtime was stopped and `C:\Videoprocessor\vp\VideoProcessor.exe`
was restored afterward; active configuration and installed binaries were preserved.


Validation: the complete native suite ran 2,435 tests: 2,434 passed and one newly
added no-bars fixture failed because it incorrectly required `FULL_RASTER_TRUSTED`
for a very dim full-raster pattern. Its actual conservative result was
`UNAVAILABLE`. The fixture now asserts the intended safety contract explicitly:
no trusted bar-crop classification, no trusted vertical bar axis, neither edge
trusted, and no distributed-picture exception. No existing assertion was relaxed.
The same new test was strengthened with bright credit strokes that pass tile
texture but lack broad median structure, plus deterministic noise spanning
codes36..72. A clean test rebuild and final affected run passed 741/741, including
that corrected fixture and the strengthened negatives. The two exact-capture
replays also passed, including startup recovery with presentation repeats.

Final serial x64 Release solution build completed successfully after the affected test run. The installed runtime was restored after isolated live verification; these changes have not been deployed.


## Continuous-subtitle AR acquisition (2026-10-13)

A full subtitle scan may now use physical top/bottom bar planes measured from the
same native analysis frame while shared presentation geometry is absent or stale.
That permission does not publish crop authority or relax glyph admission. It
breaks the dependency in which scanning needed a crop and the crop needed a
subtitle-free frame.

The subtitle worker produces a separate, exact-frame AR certificate. Accepted
glyph ownership is unknown to bar sampling, never synthetic black. Only measured
per-line panel interiors may be excluded; the cleanup union rectangle cannot hide
real picture in gaps between separate panels. Remaining samples must establish
both physical bars, neutral and consistent black levels, distributed left/right
support, picture-side contrast, and no connected picture crossing the boundary.
Large captions are supported by remaining evidence, not a small fixed box-width
limit. Insufficient unoccluded evidence fails closed.

The certificate binds transport, analysis domain, source frame, dimensions,
script, policy and continuity. Held, partial, pending, tracked-edge and stale
observations cannot qualify. A continuity reset invalidates an independent scan
for worker reuse and permits resubmission. Merely changing the presentation
snapshot does not discard a complete independent scan of the exact same pixels.

Live AR and buffered lookahead consume the same certificate and AR-effective
darkness result. Existing distinct-source-frame confirmation, near-black episode,
retention and excluded-picture safety remain in force. The worker owns the extra
bounded sampling; the render thread only consumes completed results and never
waits for a subtitle scan. New geometry may wake AR acquisition in eligible auto
modes; stable geometry retains the normal scheduled AR cadence.

A clean serial x64 Release solution rebuild succeeded. Six focused tests passed,
including source-format parity, exact-frame/continuity rules, unsupported city
content, excessive unknown coverage, and optional
native capture replay. The actual 3840x2160 paused capture produced two subtitle
lines and a current worker AR certificate for rows272..1886 with18 unknown
samples and1902 luma samples. The bounded certificate measured0.0894ms; the full
worker scan measured13.5999ms (one diagnostic sample, not a sustained benchmark).

The baseline also acquired this captured frame after restarting. Its successful
replay therefore establishes current-frame proof and extraction, not by itself
proof that every continuous-playback handoff has been reproduced. Synthetic tests
separately cover absent/stale presentation authority and changed current bars.


Final validation: the full native suite completed 2,440 tests: 2,438 passed.
The unchanged sustained converter timing test failed while compilation overlapped;
its isolated rerun passed (r210-to-RGB48 p95 approximately 4.3ms). The second failure
was a new disjoint-panel fixture that incorrectly required raw crop authority in
the presence of a deliberately connected bright stripe. Its corrected assertion
requires PROVISIONAL/BAR_PICTURE_CONTINUATION and retains the exact rejection from
the new proof. Additional transition fixture assertions were corrected to inspect
published `decision.bounds`; `stableBounds` intentionally identifies the previous
presentation during publication. No production safety rule was relaxed.

The final nine focused tests all passed, including a genuinely worker-measured
60%-width opaque card, a six-frame continuous-caption sequence through initial
acquisition and changed AR, stale identity mutations, and visible picture between
disjoint measured panels. The final affected suite passed 748/748 after these test
corrections. Production source and release binaries did not change during fixture
correction. Test artifacts are under:
`E:\codex\diagnostics\continuous-subtitle-ar-20261013\tests`.

The isolated live candidate also verified runtime consumption: at source sequence38
it used subtitle AR route4; after a startup reset it acquired physical geometry
`0,272..3840,1886`, applied scope at sequence316, and settled with a70-pixel
subtitle-preserving translation at sequence318. Both subtitle lines remained
current; the settled trace reported no subtitle losses or flashes. This validates
the paused source and wiring, not every possible movie transition. Full trace:
`E:\codex\diagnostics\continuous-subtitle-ar-20261013\live-verification.log`.
The temporary player was closed after verification. Installed binaries and user
configuration were not replaced; this change is ready for requested deployment.


## Distributed boundaries with continuous subtitles (2026-10-13)

The Eternals paused-frame reproduction exposed two independent gates. A dark
outer flank displaced the subtitle-only bar proposal although other columns
showed a common sharp boundary. Separately, the native horizontal axis remained
unresolved because picture at the left edge was dark. Waiting for a subtitle-free
frame could therefore delay AR acquisition indefinitely on a paused caption.

The ordinary flank extractor remains the first path. Only a failed edge tries
one additional candidate: the median of the densest native run cluster. It needs
at least 32 matching columns, 16 abrupt transitions, two populated spatial zones,
and a transition span of at least one third of the sampled width. Its sampled bar
must be at least 97% black and retain the connected-intrusion veto. A recovered
edge is accepted only with a separately measured, near-symmetric opposing edge;
no geometry is mirrored from a caption or the other bar. Ordinary asymmetric and
single-edge behavior is unchanged. This scan remains independent of subtitle
recognition and shared presentation authority.

The current subtitle AR proof can also use the existing vertical-only admission
contract when native side-bar evidence is unresolved. It preserves every source
column. The existing strict excluded-band profile now optionally treats only
exact-current detector-owned glyph samples as unknown; it never masks a caption
rectangle or gaps between cards. Every sampled remaining pixel must still match
the black/chroma reference within four analysis codes. Every sampled row must
retain a third of the samples and support at both flanks.

On this glyph-masked route, an explicit geometry-bound flag allows two native
scan steps of outward guard (eight rows at 2160p). This retains the capture's
filtered chroma fringe without relaxing the black threshold. The old unmasked
profile still has its one-step bound. The flag participates in evidence equality;
source identity, continuity, policy, script, distinct-frame confirmation, and
near-black admission remain unchanged. Live and buffered consumers reuse the
same worker certificate.

Native capture and diagnostic artifacts are kept outside source in
`E:\codex\diagnostics\subtitle-ar-review-20261013`. The optional replay supports
`VP_NATIVE_EXPECTED_SUBTITLE_AR_BOUNDS="268 1892"` for checked subtitle acquisition
and fresh-frame transition publication on this capture. It does not add captured
movie pixels to the repository. Deployment remains a separate requested action.

Validation: the complete native suite passed 2,446/2,446. Following diagnostic
cleanup and extra assertions, the final affected suites passed 720/720. The final
x64 Release solution build succeeded. MSVC incremental test debug-information
corruption was cleared with a clean test-project rebuild; no source safety rule
or existing test expectation was relaxed to resolve it.

The saved Eternals frame now detects both lines, measures physical bars at
276..1884, and publishes the guarded vertical-only crop 0,268..3840,1892 through
the normal four-frame acquisition model. Its strict profile has zero mismatches
(maximum remaining luma/chroma deltas 1/3). The bounded AR proof measured 0.81 ms
and the complete worker scan 16.26 ms in one replay; these are diagnostic samples,
not a sustained throughput claim. The earlier continuous-caption capture still
publishes 272..1886 and takes the original full-width path. The saved city-lights
negative still produces zero subtitle lines and no subtitle AR certificate.
Logs: `final-solution-build.log`, `full-suite.log`, `final-affected.log`, and
`final-{paused,city,continuous}-replay.log` in the diagnostic directory above.

Implementation branch: `codex/distributed-bar-proof-20261013`, based on the fetched
`origin/v1.4.01-beta` tip `facf907d`. Existing DV/recovery changes were carried from
the prior worktree using a preserved patch; the original worktree is untouched.
The installed application and active settings were not replaced during this
implementation.

## Paused-frame handoff and format consistency (2026-10-14)

### Evidence and limits

The installed session before capture had periods of 360 scans per 120 displayed
frames (72 scans/s on a 23.976 source), with every presentation pending despite
completed worker results. A window resize/queue overflow preceded the last such
period. The log did not include the worker keys needed to prove its exact cause.
After a diagnostic restart, the saved frame had no caption, native vertical bars
at 278..1884, and independent subtitle inspection bars at 277..1884. It acquired
scope at sequence 46: startup supplied usable pixels at sequence 33, followed by
the existing ten-frame bootstrap and four-frame native confirmation. It then
consumed approximately one scan per frame with no pending results. This validates
that captured state; it does not reproduce the preceding caption-bearing moment
or establish that every observed delay is resolved.

Artifacts, private pixels and logs remain outside source:
`E:\codex\diagnostics\paused-format-review-20261014`.

### Current VP Renderer analysis inventory and source contract

| Evaluation | Source and relationship |
| --- | --- |
| Live automatic AR, NLS and crop retention/release | `PrepareFrameAnalysis` -> `AnalysisLumaSource`, captured source coordinates. |
| Buffered AR lookahead and expansion | Same source contract, separately bound to each future frame identity. |
| Subtitle discovery, bar proof, glyph/card extraction and reconstruction evidence | Same captured source; independent bar discovery can precede presentation crop authority. |
| Subtitle-assisted AR | Current detector-owned ink can explain unknown samples; all remaining excluded-band pixels must still pass the crop checks. |
| Scene cuts, global/AR darkness and local boundary diagnostics | Same source view as that frame's live AR; never rendered or tone-mapped pixels. |
| Learned aspect return and boundary history | Same current pixels plus separately qualified source-scoped history; history nominates but does not erase unsafe pixels. |

Both live and queued profiles now select supported native capture adapters.
UYVY/HDYC retain 4:2:2 samples, V210 retains native 10-bit samples, and supported
RGB layouts retain per-pixel chroma. R12B/R12L now use the existing packed native
readers with correct full-range, 12-bit storage metadata. Valid DV uses the owned,
decoded 12-bit 4:2:2 buffer, sampled at its established upper-ten-bit precision.
Unvalidated DV transport is never interpreted as ordinary RGB; the existing
explicitly proved SDR tunnel fallback remains separate.

All listed VP Renderer evaluators consume the same `AnalysisLumaSample` contract (10-bit luma/chroma
codes, native coordinates) for a given captured frame. This is a shared sampling
interface, not a claim that different encoded SDR/HDR pictures have identical
numeric luminance or that RGB 4:4:4 equals subsampled YUV. Source range, matrix,
transfer, depth and interpretation remain explicit. Rendering may still use P010,
P210, native RGB or higher precision DV independently; output choice cannot change
analysis samples. No additional full-frame conversion or reduction is introduced.

This deliberately changes the old live non-DV source choice when rendering used
P010/P210. P010 vertically averages chroma, whereas native capture retains it.
Tests that asserted differing live/queued domains were replaced with the new
explicit invariant; detector thresholds and result expectations were not relaxed.
Missing native pixels, unsupported encodings and invalid DV contracts return
unavailable. Future formats need an explicit source adapter plus parity tests;
they do not silently borrow an output conversion. Direct P010/P210 diagnostic
samplers remain available for inspecting already-decoded fixtures.

This is not an application-wide migration of the legacy DirectShow/madVR path.
`CBufferedLiveSourceVideoOutputPin` still releases the captured buffer before
performing its P010-based scene detection, active-picture analysis and subtitle
processing. `FrameProcessor::AnalyzeScene` also retains its P010 interface there.
Moving those consumers requires an explicit source-lifetime and queue contract;
silently changing their pointer interpretation would be unsafe. They are outside
the active VP Renderer path tested by the paused capture and are a remaining
consistency gap. GPU subtitle extraction/compositing and repair likewise read
render storage by design; they implement display operations rather than granting
source AR authority. No claim of universal future-format support is made.

### Exact-frame worker handoff

A separate measurement flag records when no usable shared picture authority was
available and the worker used only current physical-bar extraction. That is not
positive two-bar evidence. Previously an unsuccessful or one-edge scan could not
be rebound because rebind required positive independent two-bar evidence.

Such a result can now rebind between two still-unavailable shared snapshots, and
identical active/pending work can coalesce. Exact frame, dimensions, analysis
domain, policy, script and continuity remain required. Newly available authority
requires another scan; tracked, refreshed and dependent observations retain their
old checks. No crop proof, remembered geometry, or temporal votes are invented.
This is a reproduced handoff defect; historical logs alone do not prove it caused
all of the observed pending periods.

### Learned ratios and sampling

The guarded remembered-edge path already stores up to three qualified geometries,
with overflow/retirement safeguards and families covering 1.85/1.90, 2.00, 2.20,
2.35/2.39/2.40, 2.55 and 2.76. Qualification requires native observations in two
scenes. One matching edge may nominate a learned return, but current pixels must
prove the removed bands safe. A paused cold start with no history cannot qualify
a movie's ratios by repeating the same frame. No catalog-ratio inference was added.

Native AR examines 48 samples across candidate rows/columns. Side validation uses
three inward columns across four height zones with twelve samples per zone.
Independent subtitle-bar extraction uses 128 columns across 96% of width and
scans bar depth with source-row refinement. The existing vertical-only route keeps
all source columns when side evidence is unresolved. These samplers and the
previous distributed-boundary thresholds are unchanged by this work.

Implementation branch: `codex/paused-format-review-20261014`, based on the fetched
current beta/default `v1.4.01-beta` at `facf907d`. The already tested DV/recovery
and distributed-bar work was carried intact via an external patch from its prior
worktree, which remains untouched. No deployment or user configuration edit is
part of this review task.

### Final review and validation (October 14)

An independent architect reviewed the source-selection and worker changes. The
final worker adjustment also ignores changes to an incoming predecessor reference
only when that exact-frame cold scan never consumed it. Exact identity, policy,
continuity and discontinuity checks still apply; tracked, refreshed and pending
observations cannot use this shortcut. Newly available shared authority still
requires measurement against that authority.

- Full x64 Release solution build succeeded, including a final build after the
  last worker adjustment.
- Full suite: 2,454/2,454 passed in 9.63 minutes, including GPU reconstruction.
  This run preceded the last narrow predecessor-reference reuse adjustment.
- Final affected suite after that adjustment: 725/725 passed in 36.93 seconds,
  covering analysis formats/DV, subtitle measurement and lookahead, active-picture
  evidence, remembered ratios and source crop policy.
- Final private native replays: 3/3 passed. The paused capture asserted native
  scope bounds 278..1884 and zero subtitle lines; the continuous-caption capture
  asserted two lines and subtitle-assisted AR bounds 272..1886; the city-light
  negative asserted zero subtitle lines and no subtitle-derived crop authority.
- `git diff --check` passed. No captured pixels are committed.

One new R12 fixture initially failed because its 8x2 dimensions violated the
existing DisplayMode minimum. The fixture now uses 104x100 with the packed stride;
the production code and pixel expectations were not weakened. An intermediate
test-only link needed explicit SolutionDir, and a subsequent incremental link
reported MSVC debug-information corruption. A clean test-project rebuild resolved
that, and the final solution build completed successfully.

The older October 13 `subtitle-ar-review` capture path had already been overwritten
before this investigation. It no longer contains the previously logged caption
scene, so it cannot revalidate that historical result. Its failed positive-caption
assertion was retained as diagnostic evidence rather than changing the expectation
to force a pass. The three final replay inputs were copied into a separate private
`fixtures` directory that the live capture process does not write.

Logs and private fixtures are under
`E:\codex\diagnostics\paused-format-review-20261014` (`full.log`,
`final-build.log`, `final-affected.log`, and `final-replay-*.log`). The current
paused frame contained no caption, and these checks do not establish that every
historical caption-dependent transition delay has been eliminated. The installed
application and user configuration were not replaced; the new candidate is built
for subsequent interactive review.
