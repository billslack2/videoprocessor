# VP-0211: Configurable presentation depth and display-driven vsync output

## Status

In Progress

2026-10-26 reopened after a critical live report: the Epson sometimes displays
one frame and then freezes while the desktop monitor continues rendering.
The user confirms `present_every_vsync: false` keeps Epson playback moving,
including with Config open. Configuration-window causality remains unconfirmed. Deployed host and renderer identify clean
`376e46a3`, including the accepted Ben-Hur subtitle correction.

Four preserved recovery events (20:27:22, 20:28:22, 20:36:42, 20:37:51 EDT)
show the Epson expected near 23.976 Hz, DXGI feedback near 59.95 Hz, and a
cadence recovery immediately changing FLIP to BITBLT. Source analysis and
GPU submission continue afterward. These logs do not themselves prove
physical display refresh or a deadlock.

The recovery's local `CComPtr<IDXGISwapChain>` survives across
`RecreateSwapchain`, keeping the old HWND's flip chain alive. The bundled
library silently falls back to bitblt when creation of another flip chain
fails. A scoped production output probe now releases all temporary COM
references before recreation, retaining the existing output-identity and
bounded-recovery checks. Recovery telemetry records the presentation model
before and after the rebuild. Subtitle and aspect-ratio logic are unchanged.

A hidden-window reproduction passed 32 matrix cases across bundled APIs
360/377, WARP/hardware, both attached monitors, and borrowed/owned query
routes: the old lifetime reproduces bitblt fallback; the corrected lifetime
preserves flip presentation through repeated recreation. WARP correctly
rejects unavailable physical-output verification. The initial native fixture
assumed FLIP_SEQUENTIAL; vstest legitimately selected FLIP_DISCARD, so its
final assertion accepts either initial flip mode and requires exact mode
preservation. Final clean source `bc060dba1266e7664c30f846fcbc0e0bf967e895` is pushed in
[draft PR #211](https://github.com/billslack2/videoprocessor/pull/211), based
on current beta `37b8720a`. Full x64 Release solution build succeeds. Final
cadence/lifecycle suite passes 123/123; qualification covers 190 distinct
passing checks including motion/cache and accepted rounded-subtitle GPU
coverage. The earlier broader run passed 188/190; its only two failures were
the corrected new fixture assertions, with only test code changed afterward.
All 32 isolated matrix cases also pass. Matching host, standard/beta renderer,
and test binary hashes are verified against generated Release sidecars.
[Qualification receipt](../assets/VP-0211/render-freeze-20261026/validation.json).

Deployment was offered after qualification and awaits the user's answer.
The installed `376e46a3` build and user-selected VSync-off workaround remain
in place. Live Epson scanout validation with VSync on is still required;
this draft is not merged and the story remains In Progress.

Local raw incident logs and copied configuration are retained only under
`E:\codex\diagnostics\render-freeze-20261026-2041`.
[Compact incident timeline](../assets/VP-0211/render-freeze-20261026/incident.json).

2026-10-25 PR #204 merged at the user's explicit request into current beta
`v1.4.02-beta` as `81ac960fbd62619374fba1c16630b8feac364d59`.
Tested feature head `6f6c0d2e58c2ea732bbe192ef5c7261d8df7dbbb`; unchanged
integration base `8c4537462bbabd6ce07ba3e9ffd9be9bc8745492`. The merged source
tree is identical to the tested feature. Existing clean x64 Release build
passed with zero errors; recorded artifact hashes and generated host/renderer
identity were rechecked. A fresh focused suite passed all 156 cadence, motion,
queue, telemetry, temporal-ledger and reset tests. Guarded Prepare/Merge
verified exact head/base and GitHub check state. This authorized merge
supersedes the historical draft/no-merge statements below.

The evening recurrence was on deployed `934489bb`, whose Config-only lineage
omitted the earlier locally deployed #204 fix. Direct source and ancestry
comparison confirmed the omission. Smooth Motion was Auto but bypassed in the
captured session; every-refresh owned cadence. This does not establish a failure
of #204 while present. Detailed incident logs remain local.

Future builds carrying the current Config/UI work must rebase onto this beta
or newer and verify that merge `81ac960f` is an ancestor before building. The
existing Config worktree is preserved. This merge did not deploy or restart VP;
the running installation still requires a separately requested build/deployment.
Remain Review for continued hardware and subtitle/profile-transition qualification.
[Merge validation](../assets/VP-0211/pr204-merge-20261025/validation.json).

2026-10-25 positive live checkpoint through 14:44:33: user reports OSD and
viewing look good and will continue visual testing. Matching clean host/renderer
62122309 remains active, every-refresh on, Motion off, source/display ~59.941 Hz.
The 14:43:38–14:43:44 off/classic/reconstruction/clear-yellow/black/off sequence
produced one internal cadence restart per applied update, hdmi_resync=0. No
renewed cached-repeat log after startup, no renderer/capacity/cadence drops, and
no presentation errors were observed through this checkpoint. Recent GPU frame
averages were ~4.04–4.38 ms; recent frame-period overruns were zero. Retain Review
for the user's continuing visual tests, other refresh rates and longer sessions.
[Positive checkpoint](../assets/VP-0211/subtitle-phase-20261025/positive-checkpoint-144433.json).


2026-10-25 subtitle-transition candidate deployed: isolated `6f6c0d2e` in
[draft PR #204](https://github.com/billslack2/videoprocessor/pull/204), combined
`621223092dbea7e088947523a1c9f260c4bdb930` preserves the installed tone GUI.
The user confirms the earlier NLS issue was real and fixed; the remaining
subtitle-triggered failure occurs at 24/60 fps, persists with NLS off, and may
recover after seconds or minutes. The precision-only prior fix was insufficient.

Separate eligibility and retirement boundaries use a 3 ms band capped at one
quarter-refresh. A successor near the boundary cannot justify dropping its
predecessor. Raw PTS and playback speed are preserved. After one successful
changed output pays warmup, internal repeat timing, feedback and cache eligibility
restart once without HDMI resync. Both clean x64 Release builds passed (11
warnings, zero errors); 20 clock/transition and 425 exact combined-build checks
passed. Luna xhigh final review found no blocker; production transition ordering
is source-reviewed, with the full live test still the acceptance gate.

Matching host, both renderers and receipts were verified. Active configuration
was byte-preserved (every-refresh on, depth 3, Motion off), and tone GUI/session
preserved. Backup:
`C:\Videoprocessor\vp\backup-before-subtitle-phase-20261025-143944`.
Responsive PID 82768 logs matching clean host/renderer `62122309`. Subsequent
reconstruction, clear/yellow and black changes at 14:40:51–14:40:54 and NLS changes
through 14:41:04 each produced the intended internal cadence restart. No new
cached-repeat log or renderer-drop growth was observed in the sampled window.
Visual smoothness and subtitle placement remain unconfirmed; do not close Done
or merge #204 on this sample alone.
[Incident, tests, deployment and rollback](../assets/VP-0211/subtitle-phase-20261025/README.md).

2026-10-25 persistent live regression: user reports settings can recover, but
changing subtitles to clear/yellow restarts periodic repeats/drops. Deployed
2a224308 precision fix is insufficient. New diagnostics capture yellow selection
at 14:21:06 followed by paired repeat/retirement growth; queue remains five,
Motion off, source/display near 24 Hz. next_pts_lead_ms is ~0.02-1.14ms, much
larger than the prior 10us quantization allowance. GPU query examples ~5-6ms
(~13-14% of period), while CPU RenderLocked includes ~33ms of pacing/backpressure;
do not classify that CPU wall interval as shader GPU overload. Investigating
hard-boundary phase chatter with Luna xhigh regression/mpv-source reviewers.
Preserve raw source speed and subtitle fixes. New clean beta-based worktree
E:\codex\videoprocessor\display-cadence-stability-20261025 starts at fresh
8c453746 and fast-forwards to existing fix fc20e916. No new deployment yet.


2026-10-25 NLS fix ready for live retest: [draft PR #204](https://github.com/billslack2/videoprocessor/pull/204),
fix head fc20e916. All 429 selected native/subtitle GPU regressions passed;
exact clean fix Release rebuild and 16 clock tests passed. Combined test head
`2a22430849f27fef9cea3a1cca909d94a522b310` preserves installed dedc85d9 tone GUI
work plus only this fix. Its clean x64 Release build passed (74 existing
warnings, zero errors) and all 421 focused compatibility tests passed, including
bundled NLS GPU hook and NLS failure recovery. Luna xhigh review found no
remaining blocker. Supplementary duplicate GPU run was stopped after key
cases passed, with partial output preserved; it is not counted as a suite pass.

Authorized regression test deployment completed: matching host and both renderers,
Release receipts/hashes verified, backup
`C:\Videoprocessor\vp\backup-before-nls-vsync-20261025-135117`.
Config GUI/tone work preserved, active cfg byte-identical (every-refresh off,
depth 3). Graceful close left inspected player PID 20068 running, so only that
player was stopped. Restarted responsive PID 54532; fresh host/renderer identities
both confirm clean 2a224308. Initial locked-copy attempt changed no binaries.
Live NLS + every-refresh on confirmation and subtitle revalidation remain
pending; user has not confirmed subtitle acceptance. Do not close Done.
[Validation, incident, deployment and rollback evidence](../assets/VP-0211/nls-vsync-20261025/README.md).

2026-10-25 NLS correction is committed separately as `fc20e916` in [draft PR
#204](https://github.com/billslack2/videoprocessor/pull/204). A strict 0.1us
SourceDue allowance was below the observed 10us DeckLink PTS grid (518 samples,
absolute/delta GCD 10us); deterministic matched-rate selection reproduced
repeat/retirement pairs without shader work. The shared allowance is now the
larger of 10us and one source tick, capped at 100us. It retains cadence phase
and real drift; half-refresh early selection was rejected. Periodic repeat logs
now include next PTS lead, precision allowance and queued-source count.
Release build passed (10 existing warnings, zero errors), all 16 clock tests
passed, and Luna xhigh final review found no remaining concrete blocker after
correcting the temporal diagnostic call. Expanded GPU/subtitle regressions are
in progress. An isolated combined test branch preserves deployed dedc85d9 tone
GUI sources plus this fix; no deployment has yet occurred in this correction.
[Preserved incident and timestamp evidence](../assets/VP-0211/nls-vsync-20261025/investigation.json).

2026-10-25 critical live NLS regression: user confirms stutter only when
present_every_vsync is on and NLS is enabled. Current screenshot has the option
off; later saved config has motion off, depth 3, every-refresh off. Active/rotated
logs preserved: 13:18:37-13:18:56 cached repeats and source retirements increase
together at matched ~59.941 Hz despite active CPU/render time below one refresh.
Investigate cadence/feedback/source selection; do not assume GPU overload or
claim subtitle acceptance (user has not retested subtitles). New clean source
worktree E:\codex\videoprocessor\nls-vsync-stutter-20261025 starts at fresh
beta 8c453746 (includes PR202); running deployed dedc85d96 is a separate PR177
test build and must not be silently replaced by a baseline that loses it.


2026-10-25 merged at the user's explicit request: [PR #198](https://github.com/billslack2/videoprocessor/pull/198)
into current beta `v1.4.02-beta`, merge commit
`6ba412bdb2c05c6a6fb0ec2358bc84320d3521bc`. The pinned merge guard detected
concurrent beta advancement from b1415da4 to b4d0b5cf (renderer promotion #199)
and stopped the first attempt. Reviewed the new names/selection/UI changes,
merged that beta into the feature, and tested exact clean head
`b3644172abd2bad4f3e743b6a6e59a8345b561a7`: full x64 Release build passed
(25 existing warnings, zero errors), all 381 selected native tests passed,
and offscreen renderer-promotion migration/cache-selection and Display
presentation-default/save/reopen tests both passed. Guarded Prepare/Merge then
verified exact head/base and GitHub checks; no configured checks were pending.

Story remains Review for the documented hardware/cadence qualification. This
merge request did not update the running test deployment or configuration;
the previous 3520e713 deployment evidence remains its source identity.
[Integrated merge validation](../assets/VP-0211/merge-6ba412bd-20261025/README.md).

2026-10-25 live regression correction: implemented and pushed clean head
`3520e71396a3c20112e4b18c67258052af34e0e0` to draft PR #198. Actual Smooth
Motion mixing owns cadence and bypasses the every-refresh repeat clock/cache,
source normalization and stale retirement. Shared current/queued-frame WillMix
classification prevents the old cache holding or retiring an upcoming mixed
frame. Auto bypass and deinterlace-only retain repeats; depth is independent.
Full clean x64 Release build succeeded (10 existing warnings, zero errors);
375 focused tests passed, and Luna xhigh final review found no concrete blocker.
Production queue handoff and simultaneous-on playback remain live-test gates.

The authorized correction was deployed with matching host and both renderers,
Release receipts and hashes verified. Backup:
`C:\Videoprocessor\vp\backup-before-vp0211-mixing-20261025-090909`.
Active config preserved byte-for-byte (current every-refresh=false, depth=3).
VP restarted as responsive PID 84800; both host/renderer identities match clean
3520e713. Log confirms Smooth Motion at about 23.976455 -> 59.951 Hz.
Inherited minimized startup initially reported occlusion; restoring playback
stopped those messages. Startup drops are not clean cadence proof. Retest both
options on to confirm the user's reported stutter is resolved; do not mark Done.
[Correction tests, review limits and deployment/rollback evidence](../assets/VP-0211/mixing-fix-3520e713-20261025/README.md).

2026-10-25 test deployment: user authorized deployment/startup after the source
review. The matching x64 Release host, primary/Beta renderers, Config editor and
discovery binary from `727467e7` were backed up/replaced and hash-verified at
`C:\Videoprocessor\vp`; configuration help and binary receipts were updated.
Backup: `C:\Videoprocessor\vp\backup-before-vp0211-20261025-084649`.
Only [general] `present_every_vsync: true` and `presentation_queue_depth: 2`
were added; removing them reproduces the original config byte-for-byte. Queue,
lookahead and Motion remain unchanged. Started PID 27716 at 08:46:50 EDT; it is
responsive and the fresh log confirms clean host 727467e and plugin loads.
Capture started but no input frames/renderer initialization were observed, so
live output acceptance is still pending. [Deployment evidence and rollback](../assets/VP-0211/deployment-727467e7-20261025/README.md).
Earlier no-deployment statements below describe the preceding source-review
checkpoint; this deployment is now authorized and recorded, with no merge.

2026-10-25: Implemented and pushed to [draft PR #198](https://github.com/billslack2/videoprocessor/pull/198),
head `727467e7230dae48315698087caead19518ca837`. The PR retains its original
remote branch `codex/display-vsync-20261024`; the clean implementation worktree
and local branch below preserve the same head. Current-beta base remains
`b1415da4e2cc12807f390f4b722c459be9a17c08`.

Both independent global Display controls are implemented: depth 1..8 (default 2)
and every-refresh presentation (default false). Depth is verified by DXGI
setter/readback before depth-plus-one flip buffers are created. CPU sleep pacing
is removed; the shared slot estimate uses interval-1 Present backpressure and
optional guarded DXGI feedback. Normalized fallback handles broken timestamps;
ordinary and temporal repeats use owned final pixels without expanding nominal
deinterlace work or enabling blending.

The exact committed head built successfully in x64 Release, including both
renderer variants. All 373 selected native regressions passed (19.8 s); the
offscreen Display default/save/reopen test passed. A 720-Present hardware smoke
probe covered both ownership paths at depths 1/2/8, with all Presents S_OK and
buffers 2/3/9. API-successful frame-statistics calls returned zero timing fields,
so guarded feedback correctly stayed inactive. This is not physical display,
queue occupancy or end-to-end latency proof.

Three Luna xhigh reviewers compared pinned mpv, bundled libplacebo 7.360.1 and
7.377.0, upstream APIs/plplay and regressions. Concrete findings were corrected;
final regression review approved the reviewed paths. [Source comparison, fixes,
probe source/data and validation limits](https://github.com/billslack2/videoprocessor/blob/727467e7230dae48315698087caead19518ca837/docs/VP-0211_PRESENTATION_REVIEW.md).

Remaining gate: qualify live capture, actual cadence/latency, source continuity,
output/profile/monitor transitions, stalls and shutdown against matched baseline
with both controls independently off/on. The intended limit is bounded queue
lead, not arbitrary-stall protection or exact madVR parity. No deployment,
active configuration changes or merge occurred. Do not close Done on API smoke
or source review alone. Next action is the recorded live qualification matrix.

### Implementation start and readiness

2026-10-25: User authorized implementation and primary-source comparison with
mpv/other libplacebo players, using Luna extra-high reviewers. Current GitHub
default/latest beta was rediscovered and fetched as `v1.4.02-beta` at
`b1415da4e2cc12807f390f4b722c459be9a17c08`. Source worktree:
`E:\codex\videoprocessor\vp0211-presentation-20261025`, branch
`codex/vp0211-presentation-20261025`, started clean at that beta and fast-forwarded
to the existing #198 candidate `10a4aa9a`. PR #198 remains draft; implementation
will extend/revise that candidate. No deployment is authorized.

Readiness review: the existing global config schema, Apply policy, Display UI,
GPU-owned repeat cache and single render/present owner are identified. Source
PTS must remain authoritative without audio retiming. Three Luna xhigh reviewers
are researching mpv scheduling, libplacebo D3D11 queue/pacing semantics and draft
regressions. The first implementation phase qualifies actual queue/blocking
behavior and then chooses the smallest supported timing change; unverified phase
or depth assumptions are not accepted as production behavior. Findings, probe
results and any remaining gate will be recorded before final review.

Created 2026-10-25 at the user's request to track both frames presented in
advance and presentation on every display refresh. The initial story task recorded the
requirements and verified baseline without runtime changes. Implementation is
now authorized as recorded above; merging, deployment and active configuration
changes remain separate from this source implementation.

## Objective

Give VP Renderer two independent controls under Display: how much presentation
work may be in flight, and whether output runs at every display refresh even
when the source picture has not changed. Improve resilience to short scheduling
or rendering stalls while keeping added latency explicit and bounded.

A presentation queue can retain prepared pictures until their intended vsync.
Every-refresh presentation can submit the same prepared picture on successive
refreshes. These are separate behaviors; neither requires motion interpolation.

## Verified baseline - 2026-10-25

GitHub default/latest beta is `v1.4.02-beta` at
`b1415da4e2cc12807f390f4b722c459be9a17c08`. It was fetched and inspected in a
clean detached worktree at
`E:\codex\videoprocessor\presentation-story-audit-20261025`.

| Existing mechanism | Current behavior | User configuration |
| --- | --- | --- |
| GPU presentation buffering | `LibplaceboVideoRenderer.cpp` requests `pl_d3d11_params.max_frame_latency = 2`; its VP-owned normal flip swapchain uses three buffers (diagnostic blit uses one). These are a device-latency request and buffer allocation, not proof of a constantly full two-frame queue. | Hard-coded; no presentation-depth field found in beta's configuration model or UI. |
| Capture/source FIFO | `[queue.*] queue_size` bounds capacity; `target_frames` selects VP Renderer's live/prefill target. Queue profiles also own lookahead and recovery policy. | Already configurable on Queue. These source-frame counts are not a presentation-depth control. |
| Vsync-enabled submission | The VP-owned presenter calls `Present(1, 0)`; the compatibility presenter uses libplacebo submit/swap. | Already part of presentation, but does not independently request one submission per display refresh. |
| Temporal output | `TemporalRenderer::Begin` schedules at measured display rate when Smooth Motion mixing is active; deinterlacing has its own field/frame scheduling. | Motion profiles expose `smooth_motion: off/auto/always` and mixer/deinterlacing controls. They are not an independent every-refresh switch for ordinary progressive playback. |
| Every-refresh candidate | [Draft PR #198](https://github.com/billslack2/videoprocessor/pull/198), head `10a4aa9a97e0d1cdec58cf407093c88ae524784d`, adds Display's checkbox, `[general] present_every_vsync`, and a GPU-owned final-picture repeat cache. | Draft only, unmerged and not deployed by this chat. Default false. Its ordinary-output clock uses periodic CPU deadlines plus vsync-enabled Present; it has not qualified display-phase tracking, queue behavior, or live HDMI cadence. |

Recheck these branch/PR identities before implementation. Do not describe #198
as merged beta behavior or assume that its passing unit tests establish physical
presentation timing. Reuse or revise its configuration/cache work after review;
this story owns closing the remaining design and qualification gaps.

## Configuration and operator behavior

1. Add **Presentation queue depth** to Display for VP Renderer. Proposed key:
   `[general] presentation_queue_depth`, default `2`, bounded integer `1..8`.
   This is the maximum number of in-flight presentation slots, including slots
   containing repeated pictures; it is not a guarantee of queue occupancy or a
   count of distinct captured source frames. Qualify the supported bounds before
   freezing the schema. Align swapchain buffers and retained output storage with
   the contract, or report an explicit lower effective limit and reason.
2. Add **Present on every display refresh**, independently enabled/disabled.
   Retain #198's `[general] present_every_vsync` spelling and default `false`
   unless an explicit migration is documented. Enabling it does not enable
   Smooth Motion, change its mixer, or modify the capture queue profile.
3. Use global Display scope initially, matching #198; do not silently introduce
   per-source/profile precedence. Label both controls as applying to VP Renderer.
   DirectShow/madVR retain their renderer-specific presentation settings.
4. With absent keys, preserve the current latency request and ordinary cadence.
   Round-trip values through Config save/reopen and Apply; document whether a
   depth change requires a controlled swapchain/renderer recreation. Preserve
   unrelated values, comments, unknown keys, active profile and selected output.
5. Explain the latency/resilience tradeoff in plain language. Report requested
   and effective depth separately; never equate three allocated backbuffers with
   three queued movie frames or advertise an unsupported depth as applied.

## Presentation and timing contract

- Establish one presentation owner and a bounded path for rendering/preparing
  output ahead of its display boundary. Separate costly new-source preparation
  from refresh presentation sufficiently that an already prepared picture can
  still be submitted during a slow later-source render. Define GPU/context
  serialization and safe resource reuse rather than adding unsynchronized D3D
  work on another thread.
- Queue prepared pictures for their intended output slots; submitting ahead must
  not display a future source too early. Define how repeated slots, source PTS,
  queue lead, display phase and startup latency map together. Do not assume
  FIFO order alone gives a source frame its intended duration.
- When every-refresh is on and valid output is available, use qualified vsync
  blocking, a waitable presentation mechanism and/or supported presentation
  feedback to maintain display cadence. Identify which mechanism actually paces
  each supported presenter. Nominal/measured Hz plus an independent CPU timer
  alone is insufficient evidence of display-driven synchronization. Avoid an
  additional timer fighting a blocking present path or doubling the latency.
- Select pictures using capture/source timestamps and the established bounded
  live-video delay. VP has no audio clock to resample against: do not copy mpv's
  audio-resampling or playback-speed adjustment policy. Handle input/display
  clock drift through the existing deliberate drop/repeat/recovery policy, with
  clear ownership so two cadence controllers cannot both correct it.
- With blending off, reuse the final processed pixels while a source remains due.
  Do not rerun tone mapping, source analysis or subtitle-state mutation for a
  cached repeat. For 24 fps at 120 Hz, nominal cadence is five output slots per
  picture; 24 at 60 Hz uses alternating two/three slots. Noninteger real rates
  and long-run drift must be handled explicitly.
- Integrate Smooth Motion/deinterlacing through one output scheduler. Preserve
  their intended temporal pixels, field order, geometry and caption context;
  every-refresh mode must not introduce a competing repeat loop or bypass them.
- Bound memory and in-flight work. Retained output owns its pixels and immutable
  source/generation/PTS/geometry/subtitle metadata; it cannot borrow a recyclable
  capture buffer. Define behavior when depth exceeds available live-source lead.
- Define startup, empty queue, missed deadline, prolonged input stall and recovery
  behavior. Hold a valid image according to existing policy, skip elapsed slots
  without catch-up bursts, and avoid silently increasing steady latency.
- Invalidate/retire old presentation work across source discontinuity, refresh or
  monitor change, resize, output recreation, profile/LUT/shader change and device
  loss. Stop/minimize/occlusion paths must remain responsive without busy loops,
  indefinite waits or replaying stale frames after resume.

## Readiness and bounded qualification

Before production changes, produce a reproducible non-production presenter probe
or replay that records the actual DXGI/libplacebo queue, blocking and feedback
behavior on both VP-owned and libplacebo-owned paths. Determine how a requested
depth is enforced and how output-slot timing is learned when frame statistics
are unsupported. Compare baseline, depth-only, every-refresh-only and both-on.
Record the selected design and limits. If unresolved API/concurrency behavior
could invalidate the design, create a separate bounded Backlog spike before
starting the production implementation; this root remains Backlog meanwhile.

Coordinate with:

- [VP-0208](../backlog/VP-0208_lookahead-scheduling-and-smooth-presentation.md): optional
  analysis scheduling is a separate scope; it must consume the same stable
  presentation timeline rather than extend deadlines to obtain analysis time.
- [VP-0209](../review/VP-0209_observe-output-drops-repeats-and-glitches-without-render-waits.md)
  and [VP-0210](../review/VP-0210_qualify-passive-presentation-event-collection.md):
  reuse qualified passive observation where available. They do not authorize
  render-thread diagnostic waits. Submitted output is not proof of display.
- [VP-0088](../in-progress/VP-0088_expose-fast-alpha-native-display-refresh-measurement.md):
  distinguish measured refresh from actual output-phase feedback.

## Acceptance criteria

- [ ] Both independent Display controls persist, validate and apply with their
      documented defaults, scope and restart behavior. Invalid depths/Booleans
      are rejected without partial application; existing configs still load.
- [ ] Tests prove bounded requested/effective in-flight depth, safe GPU ownership,
      correct source ordering and retirement, and the definition of depth one.
      Capture queue targets remain independently configured.
- [ ] Deterministic scheduler tests cover 24-to-120, 24-to-60, 23.976-to-119.88,
      23.976-to-120, same-rate 50/59.94/60, input/display drift, unknown timing,
      late/new/missing sources, missed slots and prolonged stalls without bursts.
- [ ] Ordinary progressive output repeats exact processed pixels without enabling
      interpolation. Smooth Motion and deinterlacing use the same timing authority
      and preserve their existing pixel/metadata semantics.
- [ ] Stress/transition tests cover a slow future render while output is queued,
      depth changes, source reset, refresh/monitor change, resize/fullscreen,
      profile/LUT change, minimize/resume, device loss and shutdown.
- [x] Record a successful x64 Release build and meaningful native/config/GPU tests
      from the pinned implementation commit before any requested deployment.
- [ ] Run matched live qualification with both settings off/on independently and
      together, including low-latency mode. Record backend, real refresh, requested
      and effective depth, queue age/occupancy, CPU/GPU tails, submission intervals,
      source continuity, intentional repeats and added capture-to-output latency.
      Define test durations and timing tolerances before comparing results.
- [ ] Distinguish source loss, successful Present, observed display/discard and
      physical HDMI/scanout evidence. Mark unsupported feedback as unavailable;
      neither an empty error counter nor periodic submissions proves clean output.
      Document remaining hardware qualification instead of claiming madVR parity.

## Primary references

- [madshi, 2016-08-16](https://www.dvbviewer.tv/forum/topic/37505-madvr-renderer-in-dvbviewer-nutzen/page/28/):
  prequeued frames let Direct3D/the driver present at future vsyncs; the alternative
  checks vsync position and submits shortly before a picture is needed. This
  explains the several-frames-in-advance setting, not the separate every-vsync
  checkbox's complete internal implementation.
- [mpv swapchain-depth](https://mpv.io/manual/master/#options-swapchain-depth):
  bounded in-flight work trades latency for pipelining/resilience.
- [mpv display synchronization](https://github.com/mpv-player/mpv/wiki/Display-synchronization)
  and [output loop](https://github.com/mpv-player/mpv/blob/master/video/out/vo.c):
  display-driven redraw/swap with source-frame vsync counts; a reference design,
  not an audio-clock policy to copy into live VP capture.
- Bundled libplacebo `d3d11.h` documents `max_frame_latency` as a device-wide cap
  (except waitable swapchains); `swapchain.h` documents submission/latency APIs.

## Tracker allocation evidence

At allocation, `origin/main` was `6f29b543` with 229 canonical files and 229
indexed items, maximum root 0210. No duplicate/missing/orphan IDs, child-sequence
conflicts or registry/count mismatch were found. Allocate 0211, next 0212, total
230. Historical status-format exceptions remain unchanged: VP-0070 lacks a
Status heading; VP-0088/VP-0168 use `In progress`, including VP-0168's index row.
No unrelated tracker repair is included. The dedicated tracker has an existing
uncommitted VP-0207 asset edit; this story is written in a clean main-based E:
worktree so that edit is preserved.

## Review qualification boundary

The earlier #198/10a4 baseline row records the initial candidate, not the current
PR head. The 727467e7 implementation supersedes its periodic timer and forced
deinterlace cadence. Native tests cover normal/fractional cadence, stale slots,
feedback validation, frozen/backward/nonfinite timestamps, exact cached GPU
pixels, configuration/range/public-reference checks and queue/telemetry/reset
policies. They do not exercise every production transition with a live input.
Those acceptance checkboxes remain open for the hardware qualification gate.

The selected architecture retains one render/GPU-context owner. DXGI owns the
bounded queue of submitted output; a second presentation thread or waitable-chain
model was not needed for exposing the existing device cap. Present returns may
queue early; conditional buffer count and unknown occupancy are logged honestly.
Both backends' source contracts and the two-path smoke probe support this choice.

## Deployed follow-up vsync fixes merged - 2026-10-26

Requested three-branch integration PR #207 merged into v1.4.02-beta as 87f61f77d3d95ddbfc1eeae343501d763d49e80b. It includes vsync recovery 28b0e42a through subtitle/vsync head e8bc0638, the retained Config UI, and Apply behavior. All three original heads are beta ancestors, and beta's final tree equals successfully tested/deployed b3b36c1d exactly. [Merge receipt](../assets/VP-0216/three-branch-merge-20261026/merge-receipt.json). Existing focused timing/subtitle validation is retained; no new physical-display acceptance or deployment is claimed. Story remains Review.
