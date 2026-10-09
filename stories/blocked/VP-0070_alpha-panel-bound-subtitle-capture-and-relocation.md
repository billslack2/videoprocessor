# VP-0070: CIH bar/boundary subtitle capture and relocation


## Scope onset presentation flash corrected locally - 2026-10-09

The user reproduced the four-sided black-box flash in both moved and Classic
modes. The recording `2026-10-09 18-53-36.mp4` visibly narrows at about
20.58-20.65 seconds. At 18:53:57 source sequences 2859/2860 entered full-raster
presentation while dense translation was confirming 1/3 and 2/3; sequence 2861
restored scope. Classic repeats the same sequence at 19:03:46, frames 598-600.
The retained native scope geometry remained available. This was an episode
presentation-policy override, not failure to capture caption glyphs.

The shared near-black episode now yields to existing bounded-confirmation crop
handling when current physical bars re-prove the exact retained scope contract.
It requires same generation/epoch, a current finite pending translation, and
an existing RETAIN_CROP episode. It cannot recover an already-full-raster
episode before confirmation. Full-raster authority, incompatible bars, stale
samples and conflicting FIT continue to reject it. Normal crop and admission
still decide final presentation. Subtitle detection/rendering did not change.

The x64 Release solution build succeeded and **2,345 native tests passed**.
Two new regression tests cover the complete three-sample onset through final
crop admission, evidence loss, and stale/conflicting/full-raster negative cases.
[Verification and exact source/binary hashes](../assets/VP-0070/scope-onset-20261009/scope-onset-verification.json).
[Moved and Classic logs](../assets/VP-0070/scope-onset-20261009/scope-onset-log-evidence.txt).
[Recorded before/during/after frames](../assets/VP-0070/scope-onset-20261009/scope-onset-before-during-after.jpg).

Candidate remains local on `codex/subtitle-held-authority-20261009`, based on
`73953a364058b1cf6e5875dfc62e32df4e3bdcd5`, including the earlier deployed
shared-authority fix. This follow-up is not deployed or merged; live HDMI
verification is pending. No active configuration was changed.

## Shared subtitle authority recovery deployed - 2026-10-09

Implemented on `codex/subtitle-held-authority-20261009`, based on beta merge
`73953a364058b1cf6e5875dfc62e32df4e3bdcd5`. Both Classic and moved modes use
fresh physical-bar reproof against retained same-generation scope geometry.
Recovery requires two current edges, compatible bounds, and no lost picture
rows; full-frame classification, missing/black evidence and moving-picture
transitions cannot borrow stale scope. The existing dense translation
confirmation remains mandatory before ending a conservative near-black episode.
Normal crop/admission checks still apply. No forced aspect or detector threshold
relaxation was introduced.

x64 Release build succeeded. All **2,343 native tests passed**, including three
new policy tests; four screenshot recovery/detection replay cases passed.
[Source/binary hashes and verification](../assets/VP-0070/held-authority-20261009/subtitle-held-authority-verification.json).
[Deployment and backup](../assets/VP-0070/held-authority-20261009/subtitle-held-authority-deployment.json).
Host and renderer deployed together, with runtime metadata and configuration
preservation verified. This is a local working-tree update, not a new release.
Live HDMI replay still needs confirmation in both styles; re-enter the scene
from established scope playback rather than relying only on startup at a paused
caption. Source hashes identify the exact candidate. Parent status remains open.

## Scope replay confirms authority gating - 2026-10-09 18:27

User repeated the missed captions with scope enabled. New logs confirm
`screen_aspect=2.35000`, retained trusted geometry `0,270-3840,1886`, but
candidate `0,276-3840,1902` rejected as `bar-asymmetry`. At 18:25:40 the shared
subtitle action is temporarily held through an authority gap; at 18:25:41 that
hold releases. At 18:27:12-22 BBOX repeatedly reports `not-analyzed` and
`shared-classic-authority-unavailable`, while display owner is `fit` with
`0,142-3840,1918`. Direct screenshot glyph tests still pass 4/4.
This new scope replay supersedes the earlier inconclusive 16:9 session: the
failure occurs before glyph analysis, in shared framing/authority admission.
No fix is included in RC2.1. Investigate safe continued analysis against retained
trusted bar geometry during ambiguous evidence without bypassing genuine picture
expansion, scene-change protection, or shared Classic/moved policy.
[Scope log excerpt](../assets/VP-0070/rc2.1-73953a36/missed-captions/scope-log-excerpt.txt).

## Published RC2.1 and missed-caption investigation — 2026-10-09



Merged [PR #136](https://github.com/billslack2/videoprocessor/pull/136) into

`v1.4.00-beta` at `73953a364058b1cf6e5875dfc62e32df4e3bdcd5`.

Published [1.4 RC2.1](https://github.com/billslack2/videoprocessor/releases/tag/1.4-beta-rc2.1):

shared Classic aspect authority, false-scene rejection, stable caption backing.

[Artifacts, checksums and qualification](../assets/VP-0070/rc2.1-73953a36/release.json)

and [verification receipt](../assets/VP-0070/rc2.1-73953a36/release-receipt.json).

All 2,340 native tests and 10 Config tests passed, plus packaging/runtime/portable checks.

Unsigned prerelease. Isolated installer refused running VP/Config; full lifecycle,

clean Windows and interactive Config qualification remain unverified. No deployment

or interruption of user playback was performed by this release workflow.



New user screenshots “Those are the forward stabilizers.” and “And those two

control the pitch?” reproduce successful direct glyph detection (4/4 boundary

variants). [Saved fixtures and results](../assets/VP-0070/rc2.1-73953a36/missed-captions/results.json).

The paused live log at 18:18 shows `analysis_reason=not-analyzed` and

`bar_reason=shared-classic-authority-unavailable`. User clarified that the display

was set to 16:9 to capture screenshots. Logs confirm `screen_aspect=1.77778` and

`automatic crop is off; preserving full raster`. This session does not establish

a subtitle miss during normal scope playback. The earlier framing-regression

interpretation was withdrawn. Both screenshots detect with supplied bar bounds;

the saved test is a detector regression fixture, not a native HDMI/full-history

integration test. No detector relaxation or aspect policy change was made.

The release does not close the parent story or these follow-ups.


## Published release — 2026-10-09

[1.4 RC2](https://github.com/billslack2/videoprocessor/releases/tag/1.4-beta-rc2)
is published as a prerelease at `abc631aadb70d692b391dfb04cc9c9459c45e1d4`.
Notes highlight Remote Config from another computer and beta subtitle moving.
[Downloads, SHA-256 hashes, and qualification](../assets/VP-0070/rc2-abc631aa/release.json).
Clean x64 Release: **2,327 native tests and 10 Config tests passed**, plus
installer/runtime/portable checks. Both Setup and Portable ZIP and their checksum
sidecars were uploaded and verified against GitHub asset digests. The distribution
is unsigned. Isolated lifecycle testing was blocked by running VP/Config; clean
Windows and interactive qualification remain outstanding. Existing user playback
and installation were preserved. This release does not close broader follow-ups.

## Beta integration — 2026-10-09

User-authorized merge: [PR #135](https://github.com/billslack2/videoprocessor/pull/135)
into `v1.4.00-beta`, merge `abc631aadb70d692b391dfb04cc9c9459c45e1d4`.
Feature `4690baafba91210b7eecd5fedfe8b8819ffa53a1` has the same tree as the merge.
The clean feature x64 Release build passed **2,327/2,327 native tests** and
**6/6 subtitle Config tests**. Includes subtitle profiles, beta reconstruction
and solid relocation, native punctuation completion, scaling/shape/offset,
black-backed continuity, and layout/performance telemetry.

[Verification and binary/log hashes](../assets/VP-0070/beta-4690baaf/verification.json).
Prior screenshot replays cover 18 resolution/mirror variants; thunder recording
retains 43/43 caption samples with no extra detections. These are rendered SDR
replays, not full HDR HDMI qualification. Live stability and broader parent
acceptance remain open; this merge does not close the blocked follow-up tasks.
No installer, portable ZIP, or GitHub Release was published by this merge.
The user's existing installation was not changed again during integration.

## Historical status — 2026-10-03

Blocked (2026-10-03), unchanged. Follow-up adds current-ink-verified rescue for at
most two missed source frames, 10px outward box padding, and optional current-frame
rectangular cut/paste with a stable 15px inset above the active-picture bottom.
This copies picture pixels inside the rectangle and clears exposed source black;
it is an experiment, not production glyph extraction/restoration.

Saved locally at `5ae388020caf402079f15b627d79f82f8d56c885` on
`codex/subtitle-cut-paste`, based on the freshly verified same beta tip.
Clean x64 Release: **1,990/1,990 native tests; 83/83 Config checks**. Final recording
regressions retain 807/807 sampled and 2,018/2,018 capture positives, 19 stable cue
intervals, 0/234 and 0/586 negative boxes, and valid padded/stable cut placements.
Injected one/two-frame segmentation misses retain geometry; blank controls clear.
RGB and HDR P010 GPU readback tests pass. Live capture remains unqualified.

Run either command in `E:\codex\subtitle-cut-paste-test-5ae38802`:
`START-SUBTITLE-BOX.cmd` or `START-SUBTITLE-MOVE.cmd`. Both use full-raster/no-warp
diagnostic presentation. Paired Release hashes verified; existing cfg/state
preserved and backed up; mode copies alter only the two subtitle flags.
No normal-install changes, playback launch, beta merge, or source public upload.

[Final validation](../assets/VP-0070/cut-paste-5ae38802/validation.md),
[test-copy receipt](../assets/VP-0070/cut-paste-5ae38802/test-copy.json),
[checks and hashes](../assets/VP-0070/cut-paste-5ae38802/validation-final.json),
[sampled replay](../assets/VP-0070/cut-paste-5ae38802/recording-23976.json),
[every-frame replay](../assets/VP-0070/cut-paste-5ae38802/recording-60.json),
[injected failure replay](../assets/VP-0070/cut-paste-5ae38802/recording-injected-faults.json).

## Previous Phantom detection trial (2026-10-03)

Blocked (2026-10-03), unchanged. Added the user's Phantom Menace recording as a
repeatable actual-pixel regression and iterated with three reviewers. The new
diagnostic proves bars independently, preserves partial crossings and both lines,
recovers sampled-away punctuation and rejects mismatched picture highlights.

Saved locally on `codex/subtitle-phantom` at
`db757d0de6734efc66aec99f0f8f0a5dd0da7b25`. Feature-branch public upload is pending
explicit approval after automatic review rejected it. Existing branches remain.

Pinned x64 Release: **1,966/1,966 native tests**, including **81 subtitle tests**,
and **82 Config checks** passed. Actual recording: 807/807 sampled positive frames
and 2,018/2,018 every-capture-frame positives complete; 19 annotated cue intervals
each keep one fixed box; 0/234 and 0/586 respective negative boxes. Three complete
annotated cues have no captured-frame onset delay. Sixteen other positive
intervals are interior-only, so they make no onset claim. Labels informed the
iteration; this is regression evidence, not unseen or original-4K validation.

Run `E:\codex\subtitle-phantom-test-db757d0d\START-SUBTITLE-TEST.cmd`.
Playable green-box previews and a repeatable local recording-test launcher are
in that isolated folder. Previous trial config/state copied unchanged and backed
up; paired build hashes and exact config validated. No usual-install changes or
playback launch. Broader synthetic scenery still produces incomplete/changing
boxes, so production extraction/relocation remains blocked.

[Validation and limits](../assets/VP-0070/phantom-db757d0d/validation.md),
[test-copy receipt](../assets/VP-0070/phantom-db757d0d/test-copy.json),
[sampled recording report](../assets/VP-0070/phantom-db757d0d/recording-23976.json),
[every-frame recording report](../assets/VP-0070/phantom-db757d0d/recording-60.json),
[broader corpus comparison](../assets/VP-0070/phantom-db757d0d/synthetic-comparison.json).

## Previous lookahead trial (2026-10-02)

Blocked (2026-10-02), as requested. The team implemented and independently reviewed
queued-frame confirmation, current-frame bar proof across AR changes, both subtitle
line orders, corner-title rejection and Config persistence. Saved/pushed at
`b1b6847fad4a0eec1dba790fe024999e7cdc153f` on `codex/subtitle-lookahead`, based on the
GitHub beta tip `c84863eaa59e03fe218de824c34b7c0220c4fd87` discovered at task start.
The original `subtitle-moving` branch is preserved.

Clean x64 Release: 1,935/1,935 native tests (including 50 subtitle regressions) and
82 Config checks passed. The new isolated test copy is
`E:\codex\subtitle-lookahead-test-b1b6847f\START-SUBTITLE-TEST.cmd`.
Current trial config/state copied unchanged and backed up; exact config validated
with production reader. No usual-install changes or playback launch.

Spatial corpus still fails complete-box/stability acceptance on busy scenery:
complete 168/198 HD and 177/198 UHD positive frames, with 0/360 negative/gap boxes
at each size. Look-ahead behavior has unit coverage but awaits live playback.
No production relocation or story acceptance is claimed.

[Validation and limits](../assets/VP-0070/lookahead-b1b6847f/validation.md),
[test-copy receipt](../assets/VP-0070/lookahead-b1b6847f/test-copy.json),
[1080p replay](../assets/VP-0070/lookahead-b1b6847f/1080p-results.json),
[4K replay](../assets/VP-0070/lookahead-b1b6847f/4k-results.json).

## Previous trial status (2026-09-25)

Blocked (2026-09-25). Keep the feature blocked for live validation, as requested.
The user reports detections across the picture and reiterates that actual
subtitle content must enter a black bar; reduced-resolution detection is enough.
Applied a scoped eligibility correction on the named branch. The live failure
is not yet diagnosed from a recording. No deployment or acceptance is claimed.

Saved and pushed on `subtitle-moving` at
`1ead05d2369169c87a5b09dbd28add99a4f0b0df`.
Worktree: `E:\codex\videoprocessor\vp-0070-bbox-test`.
Continued the explicitly named branch from 036db46e. GitHub beta was verified at
ff53b4683a2a6b59423f05918e29ce1e0c8cdca8; this correction does not merge the
newer beta into the saved experimental branch.


The current trial supersedes the historical panel/OCR design below for Step 1:
a renderer-neutral classical detector finds a bar-anchored cue and related
picture-side lines; the renderer draws one bright green 3-output-pixel outline.
No OCR, learned model, glyph extraction, source erasure or relocation is used.
The opt-in diagnostic forces full-raster presentation without crop, NLS warp or
subtitle translation. Source pixels remain unmodified. Every new source frame
is inspected; the first qualified box is shown immediately once trusted bar
geometry exists. Matching cues retain geometry; at most two missing observations
hold the diagnostic outline, explicitly logged as held rather than detected.

Original detector validation at d2b1408a (startup fix below):

- Clean x64 Release rebuild succeeded; all 1,393 unit tests passed.
- Each 342-frame 1080p/4K replay covered six real movie backgrounds with synthetic
  known glyphs: 36/36 isolated positive cue onsets had complete bounds immediately;
  all 126 positive frames had complete bounds. All 54 labeled blank/picture-only/
  menu negative frames had no fresh detection. Gap frames are excluded from that
  negative metric because the outline intentionally has a two-frame hold.
- The same two-line cue across 18 changing-background frames retained one box.
- Detector-only CPU P50/P95/P99: 1080p 1.606/2.919/3.285 ms;
  4K 1.197/2.508/2.948 ms (max 5.958 ms). These are offline measurements,
  not live frame-budget or presentation validation.
- A top-boundary case includes extra picture detail (box/glyph-envelope area
  ratio 1.534). Synthetic coverage does not establish real-caption precision,
  compressed/HDR accuracy, startup bar acquisition latency or live stability.

Deployment: backed up the host, renderer and configuration, installed the matching
Release pair, and verified both destination SHA-256 values against their build
artifacts. Appended only `[vprenderer] subtitle_bbox_test: true`; saved 16:9
viewport/zoom and other user configuration remain unchanged. VP was stopped and
was not launched. Restart is required to enable/disable the diagnostic.

[Deployment receipt](../assets/VP-0070/bbox-trial-d2b1408a/deployment-receipt.json),
[1080p replay](../assets/VP-0070/bbox-trial-d2b1408a/1080p-results.json),
[4K replay](../assets/VP-0070/bbox-trial-d2b1408a/4k-results.json).
Backup: `C:\Videoprocessor\vp\backup-before-vp0070-bbox-20260920-204651`.

Live review: start VP in 16:9 (F3), play real subtitles and assess the first
complete box, late expansions, missed lines/words, false positives and coordinate
drift throughout each cue. `SUBTITLE BBOX` evidence is in
`C:\Videoprocessor\vp\logs\vp_debug.log`. Disable the trial by setting
`subtitle_bbox_test: false` and restarting. No production or root-story completion
is claimed. Historical requirements below are retained for context and require
reconciliation before later steps; they do not mandate OCR or opaque panels for
this authorized detection-only trial.

## Bar eligibility and reduced-resolution detection (2026-09-25)

Reproduced a false box with picture-only glyphs, a small boundary fragment, and
rejected isolated bar pixels. The former check counted every bright mask pixel
inside a line rectangle, including rejected components. Eligibility now sums
bar pixels belonging to the accepted components of that specific line. Padding
and discarded pixels cannot provide bar evidence. This closes a proven failure
path; accepted shapes can still be non-text, and the user's live report has not
been attributed conclusively without a recording.

An outline clears immediately when its accepted bar anchor disappears, even if
picture-only text remains. The maximum two-frame diagnostic hold requires a fresh
bar anchor inside the old cue. Fixed top-boundary sampling so one sampled glyph
row inside a real bar qualifies immediately.

UHD already used stride four (960x540, one quarter per axis, one sixteenth of
pixels). The limit now explicitly covers both axes. Only bar/boundary strips are
sampled; rendering stays full resolution and logs report the actual stride.
The global active-picture analyzer is unchanged.

Validation: clean x64 Release build succeeded; all 17 subtitle tests passed.
Both 342-frame replays retained 36/36 complete isolated cue onsets on their first
frame, all 126 positive frames complete, 54 labeled negatives without fresh
detection, and one unchanged two-line box across 18 changing-background frames.
P95 detector-only CPU: 2.076 ms at 1080p and 2.067 ms at 4K. The previous oversized
top-boundary case and real-caption precision remain open. Synthetic replay is
not live acceptance.

Full-suite qualification is NOT clean: 1,397/1,398 passed at the pinned commit,
with ProfileChangeDisplayDurationIsBoundedAndLive failing. An isolated TEMP run
passed 1,395/1,398, also failing SameSizeEditWithRestoredWriteTimeInvalidatesCache
and UnifiedProfileRuntimeReloadsEditedViewportAndKeepsSelection. All three passed
when run separately; root cause is unproven. No unrelated cache implementation
was changed. The first development run also caught a hold fixture whose companion
accidentally crossed the bar; it was corrected before the final commit.

[Validation](../assets/VP-0070/bar-proof-1ead05d2/validation.json),
[1080p replay](../assets/VP-0070/bar-proof-1ead05d2/1080p-results.json),
[4K replay](../assets/VP-0070/bar-proof-1ead05d2/4k-results.json).
No deployment or configuration changes were made for this correction.

## Startup correction and redeployment (2026-09-20)

The user reported a fatal startup error for `subtitle_bbox_test`. Deployed host
and renderer hashes still matched d2b1408a: the binaries had not been overwritten.
The trial omitted both canonical and target-model startup schema validation for
the new boolean setting. Corrected in `036db46e213835b861f181fab1a951b7afc94fe6`
on the same feature branch; beta remains b3c0b3a6.

The expanded startup-reader regression initially reproduced both validation
failures. The corrected clean x64 Release build passed all 1,394 tests, including
true/false acceptance and invalid-value rejection. A temporary read-only probe
also passed the actual deployed configuration through RendererProfileConfig::Read
and confirmed the enabled diagnostic flag. Detector code is unchanged.

Redeployed the matching host/renderer Release pair, backed up both originals,
and verified destination hashes. Configuration is unchanged. VP was stopped;
playback/startup in the GUI still awaits the user's live trial. The earlier
statement that d2b1408a was ready for live playback was incorrect because startup
had not been validated through this schema path.

[Corrected deployment receipt](../assets/VP-0070/bbox-trial-036db46e/deployment-receipt.json).
Backup: `C:\Videoprocessor\vp\backup-before-vp0070-configfix-20260920-214602`.

## Previous blocked evidence

Blocked 2026-08-08. The first diagnostic implementation failed live validation:
it missed real compact Apple TV panels and classified a large dark picture
region as a panel/glyph mask. The available classical and off-the-shelf OCR
evidence does not yet provide a safe general glyph/subtitle classifier, so the
root and VP-0070-1 through VP-0070-5 cannot make meaningful implementation
progress. The feature remains fail-safe off and the rejected implementation
must not be redeployed.

Resume only after a detector architecture and representative offline corpus
demonstrate the parent false-treatment and recall requirements, with stable
active-picture/bar-boundary evidence, and the developer accepts that evidence
as sufficient to restart live implementation. The root closes only after the
rebuilt path has representative Alpha and DirectShow/madVR live-capture
evidence.

Build-only checkpoint (2026-08-01): `54f0e0f` on
`codex/vp-0070-1-panel-detection`, rebased through local VP-0066 tip
`f9b3ad1`, completed a clean x64 Release build and passed all 412 tests. The
new `subtitle_panel_test_mode` is fail-safe `off` by default and requires an
explicit `highlight` or `move` value; it is independent of the legacy OCR
`subtitle_reposition` path. No deployment or active-configuration change was
performed. Scene detection remains owned by VP-0066 and was not changed for
this story.

Live screenshot follow-up: a bottom-bar subtitle exposed that active-picture
evidence stopped at the first subtitle-contaminated black-bar row, so VP-0070
was disabled before glyph acquisition. The checkpoint now tolerates a bounded
caption interruption while retaining distributed black/neutral/contrast and
opposing-bar authority. A 3840x2160 P010 fixture with 276-pixel scope bars and
a long subtitle wholly inside the bottom bar now proves bar authority,
candidate/stable detection, and Highlight mutation. Fresh VP-0070 worktree
builds also copy an explicit `highlight` test setting; production code still
defaults to `off` when that setting is absent.

## User story

As an Alpha or DirectShow/madVR user with a CIH screen, I want VP to treat an
opaque-panel subtitle whose glyphs either cross into or lie wholly inside an
encoded top or bottom black bar, so off-screen captions can be recovered
without analyzing or changing ordinary subtitles entirely inside the picture.

## Input contract

This feature is opt-in and applies only while all of these are true:

- VP has a stable current active-picture rectangle and a real encoded top or
  bottom bar;
- each source caption is on an opaque, low-variance dark panel (black,
  charcoal, or gray are valid);
- the glyphs have sufficient luma or color contrast with the panel;
- locally dark opaque backing supports the line; and
- meaningful glyph-mask pixels either exist on both sides of exactly one
  active-picture boundary or lie wholly inside one encoded bar within the
  bounded search depth.

Captions entirely inside the picture, merely touching an edge through dark
backing/padding, or outside the encoded bar/boundary strips are out of scope and
must report `unavailable`. Exact original black-panel endpoints are not claimed
when they visually merge into the encoded bar or dark picture. VP instead
derives a new stable capture/destination box from the tight glyph geometry plus
deterministic padding.

If the contract cannot be established for a frame, VP passes it through
unchanged and reports `unavailable`. An optional text detector/OCR provider may
contribute asynchronous acquisition evidence, but recognized words and neural
geometry are never authoritative for panel bounds, glyph masks, capture,
inpaint, cue identity, or rendering.

## Objective

Implement a renderer-neutral panel-first pipeline for Alpha and
DirectShow/madVR that:

1. searches only bounded strips around stable encoded top/bottom picture
   boundaries;
2. qualifies locally dark opaque backing around a boundary-crossing or
   bar-contained line;
3. estimates its stable background color and extracts a tight, soft glyph mask
   from the contrast with that color;
4. requires meaningful backing-supported glyph ink to cross the same boundary
   or remain wholly inside the encoded bar;
5. freezes the panel, glyph, mask, and destination geometry for a cue;
6. uses an optional off-the-shelf text detector only to confirm/reject a new
   text-like candidate asynchronously; and
7. restores the source glyph area to its learned panel color and composites
   the captured visual glyphs onto a destination panel in the same treated
   frame.

Recognition is allowed only if a later benchmark proves a reliability benefit
that detector-only inference cannot provide. It is never run per frame and
never controls visual geometry.

## Temporal and presentation rules

- A synchronous bounded prefilter may suppress glyphs on the first candidate
  frame only after a separately measured `safeToSuppress` gate passes. No
  worker result may be awaited before present.
- A cue becomes `stable` after the configured matching observations. Each
  member's panel rectangle, glyph rectangle, background color, soft mask, and
  destination rectangle then remain immutable until cue loss or a confirmed
  cue transition.
- Per-frame validation decides only whether the same cue persists. It must not
  chase a subtitle or jitter either rectangle.
- Source restoration and destination compositing are atomic for a treated
  frame: VP must never show the source glyphs and then the moved form.
- Geometry is tied to the exact source frame, raster, renderer/video-state,
  active-picture, and viewport generations. Any mismatch invalidates treatment
  rather than applying stale geometry.
- An active-picture boundary change immediately invalidates all candidates and
  stable CueSets. Old geometry is never remapped to a new boundary.

## Decomposition

1. [VP-0070-1](VP-0070-1_panel-glyph-detector-and-contract.md)
   â€” replace the failed detector with a renderer-neutral multi-panel CueSet,
   strict top/bottom boundary-or-bar policy, benchmarked backing/glyph
   proposals, and optional PP-OCR text-proposal evidence.
2. [VP-0070-2](VP-0070-2_always-on-panel-diagnostic-overlay.md) â€”
   implement temporal cue IDs, tolerant current-frame validation, immutable
   per-line geometry, and stable-only diagnostics in Alpha and
   DirectShow/madVR.
3. [VP-0070-3](VP-0070-3_same-frame-panel-restoration-and-glyph-relocation.md)
   â€” restore only the source glyph area and composite captured glyphs into a
   stable destination panel, including the measured first-frame policy.
4. [VP-0070-4](VP-0070-4_panel-subtitle-live-validation-and-performance.md)
   â€” validate real Apple TV captures, stability, failure behavior, and the
   VP-0066 low-latency evidence.
5. [VP-0070-5](VP-0070-5_extract-subtitle-analysis-and-relocation.md) â€”
   extract subtitle acquisition, analysis, tracking, restoration, and
   relocation from the live pin behind the accepted CueSet contract.

Each later child depends on acceptance of the previous one. The root is done
only when all five are done and the input contract has been demonstrated on
representative live captures.

## Validation requirements

Use SDR, HDR, and LLDV-derived Apple TV captures at 23.976, 24, 50, 59.94, and
60 Hz. Include top- and bottom-boundary crossings and captions wholly inside
both bars; black, charcoal, and gray panels; bright, dim, outlined, and
multi-line glyphs; cue changes; picture-only captions; panel-padding-only
crossings; dark non-caption material;
and normal plus scope/CIH profiles. Retain captures, panel/glyph/destination
geometry, cue state, active-picture generations, and CPU/GPU/present evidence.

## Root acceptance criteria

- OCR/text detection, if enabled, is asynchronous acquisition evidence only;
  it never defines visual geometry or blocks presentation.
- A CueSet preserves each independently boxed subtitle line as a separate
  panel/glyph/capture member; a union envelope is non-actionable metadata.
- Eligibility requires meaningful dark-backing-supported glyph ink either on
  both sides of exactly one stable encoded-bar boundary or wholly inside one
  confirmed encoded bar.
- Stable cues retain identical panel, glyph, and destination geometry for
  their complete lifetime.
- A treated frame contains either the original untouched input or restored
  source glyph areas plus destination glyphs, never both versions.
- Missing or ambiguous panel evidence fails safe to unchanged output.
- VP-0066 queue, liveness, and latency evidence shows no new unbounded queue,
  blocking readback, sustained frame drop, or presentation regression.
