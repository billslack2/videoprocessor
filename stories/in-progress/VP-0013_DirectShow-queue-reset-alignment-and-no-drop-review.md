# VP-0013: DirectShow queue/reset alignment and no-drop review

## Status

In Progress. Review execution started July 24, 2026. This is a read-only
behavioral review of the committed principal working-tree snapshot; it must not
make queue/reset code changes until it records the current behavior, the
`origin/main` comparison, and a safe recommendation.

Readiness record:

- review worktree: `C:\Users\bslac\vp\videoprocessor - VS2026`;
- review branch: `v1.1.012-beta-ffmpeg-4.4.8`;
- immutable branch snapshot: `33e9686b9e3361296d343eeb2959f39526ea2bdc`;
- fetched comparison baseline: `origin/main` at
  `82b96f2127596ce130e80e54aa9219ba8155381e`;
- the previously messy principal working tree was intentionally committed as
  the review subject and is clean at review start;
- review artifacts will be recorded in this story, with separate linked
  documents only if the behavior matrix becomes impractical to maintain here.

First progress note: execution begins by tracing the raw and converted queue
contracts and enumerating every reset, clear, re-prime, and wake-up transition.

Implementation progress:

- `36bb7acd30d3af582dc8689e79fedce4c577ab0c` centralizes GUI lifecycle reset
  requests with an explicit reason and graph-versus-live-queue scope. It
  coalesces display/startup/resize/manual/timing-offset/queue-size requests,
  retains the one-startup-re-prime-per-new-graph guard, and makes queue
  high-water reporting diagnostic-only.
- `VideoProcessor-GUI.vcxproj` built successfully as `Debug|x64` and the full
  solution rebuilt successfully as `Release|x64`.
- The Release executable was deployed for user validation to
  `C:\Videoprocessor\vp\VideoProcessor.exe` after creating the recoverable
  backup `VideoProcessor.exe.20260724-191050.pre-vp0013.bak`. Deployment did
  not alter live configuration, renderer state, or shaders.
- Runtime validation remains: exercise startup, display/refresh transition,
  fullscreen/windowed resize, queue-size change, timing-offset change, and an
  NLS shader rule requiring media-type renegotiation; confirm one named reset
  per lifecycle incident and no reset from steady queue depth.

Interim comparison decision (queue/reset paths): do **not** restore
`origin/main`'s automatic reset behavior.  Main treats the displayed combined
queue count as one bounded queue, immediately resets at full or hard-coded
depth thresholds, resets a queue considered stuck, and resets after a resize.
The review snapshot instead evaluates raw and converted queues independently,
uses only per-queue high-water detection, defers recovery through a one-shot
timer, suppresses it during startup/pending/cooldown windows, performs
queue-only re-prime for ordinary pressure, and reserves graph reset for
startup or display/video-mode transitions.  That is the correct direction and
should be retained.

Confirmed review item for a later, evidence-backed follow-up: both main and
the review snapshot discard old converted samples when the buffering-phase
ceiling is exceeded, but this path is not included in the exported dropped
frame count.  It must be instrumented by cause before any policy change; it
is not a reason to re-adopt main's reset behavior.

Approved policy and initial implementation direction:

- Known live-HDMI lifecycle changes may deliberately interrupt delivery and
  restart from current input: manual reset, renderer startup, display or
  refresh/video-mode transition, fullscreen/windowed resize after debounce,
  timing-offset change, queue-size change, and renderer/shader changes that
  require media-type renegotiation.
- All GUI-originated interruptions are coalesced by one reset coordinator with
  a named reason and either graph-reset or live-queue-re-prime scope.  A graph
  reset dominates a pending queue re-prime.  One startup re-prime is allowed
  per newly created graph, never per reset completion notification.
- Stable playback must not reset from raw/converted depth, combined depth,
  stuck-depth samples, or an empty queue. High water is diagnostic only until
  VP can prove a local capture, conversion, or delivery-progress failure.
- The existing pin-level reset transaction remains the sole segment boundary:
  flush downstream, serialize delivery, advance epoch, discard old samples,
  reset timing, end flush, and issue one new segment.
- Follow-up observability must add per-stage progress ages, incident IDs, and
  per-cause discard counters before automatic steady-state recovery is added.

## User story

As a VideoProcessor user, I need the DirectShow capture/conversion/delivery
queues to keep the renderer supplied without unnecessary frame drops or reset
cycles. When a renderer sink (including madVR) stops filling, VP must identify
the real cause and recover only when a reset is justified. A transient or valid
zero-sized VP queue must never by itself create a repeated reset loop.

## Scope and branch

Review the actual code and working-tree changes on:

`C:\Users\bslac\vp\videoprocessor - VS2026`

Branch at story start:

`v1.1.012-beta-ffmpeg-4.4.8`

Compare the relevant behavior with the current fetched `origin/main`. This is
not a request to discard branch features or make the branch mechanically match
main. The goal is to retain intentional branch behavior while restoring or
preserving the fundamental queue/reset invariants that prevent drops and allow
the renderer sink to fill.

Primary review targets:

- `src\VideoProcessor-Lib\microsoft_directshow\live_source_filter\ALiveSourceVideoOutputPin.h`
- `src\VideoProcessor-Lib\microsoft_directshow\live_source_filter\CBufferedLiveSourceVideoOutputPin.h`
- `src\VideoProcessor-Lib\microsoft_directshow\live_source_filter\CBufferedLiveSourceVideoOutputPin.cpp`
- `src\VideoProcessor-GUI\VideoProcessorDlg.cpp`

Read the directly coupled renderer/live-source interfaces as needed, but keep
the decision centred on queue ownership, queue capacity, delivery starvation,
and reset/re-prime behavior.

## Problem statement

Observed failure symptoms have included all of the following at once:

- VP raw and converted queues repeatedly or continuously report zero;
- the renderer sink does not fill its own queues;
- presentation eventually drops frames or visibly loses smooth delivery;
- resets may appear tempting as a recovery mechanism.

Zero depth is not inherently unhealthy. It can mean normal just-in-time
conversion and immediate consumption, startup/re-prime, intentional flushing,
paused/stopped delivery, a renderer transition, or a temporarily slower
producer. Treating `raw == 0 && converted == 0` as sufficient evidence of a
fault would create a self-sustaining reset loop, keep the sink empty, and make
the original problem worse.

Current branch code already has queue-health monitoring, delayed reset/re-prime
timers, reset suppression/cooldown windows, renderer startup handling, and
separate raw/converted queue counts. Their combined behavior has changed
substantially from `origin/main` and must be reviewed as one state machine.

## Required review questions

### 1. Establish the queue contract

Document the exact producer/consumer contract for each queue:

- raw capture queue: producer, consumer, capacity, overflow action, and drop
  accounting;
- converted-sample queue: producer, delivery consumer, capacity, event and
  semaphore ownership/counting, overflow action, and drop accounting;
- DirectShow allocator/delivery and renderer-sink queues: what VP can observe,
  what it cannot observe, and why their depth must not be inferred solely from
  VP queue depth.

Clarify whether the displayed combined `raw + converted` count is diagnostic
only. Each queue has its own capacity; a combined value must not be compared to
one queue's configured maximum as though it were a single shared buffer.

### 2. Compare branch behavior with `origin/main`

Create a focused behavior matrix for the four primary files. For each relevant
method/field, record main behavior, branch behavior, reason for any divergence,
and whether the branch invariant is better, equivalent, regressed, or unknown.
At minimum cover:

- queue construction, configured maximum size, resizing, and allocator buffer
  count;
- enqueue/dequeue behavior and every full-queue/drop path;
- conversion worker wake/sleep, converted-available event/semaphore balance,
  and delivery wake-up;
- `Reset`, live-queue reset/re-prime, flush, stop, graph rebuild, and epoch or
  generation invalidation;
- GUI timer paths, renderer-state changes, display/LLDV/source transitions,
  startup delays, pending-reset flags, and suppression/cooldown windows;
- every place that clears queues, resets counters, or changes the UI/OSD queue
  report.

Do not compare only line diffs. Trace the actual transitions and identify where
one branch's change altered a prior invariant.

### 3. Define a safe reset policy

Classify every reset trigger as one of:

- **Lifecycle reset** — user action, explicit renderer restart, graph rebuild,
  or a material source/display transition. These have a known reason and may
  deliberately flush/re-prime.
- **Validated recovery** — a sustained, independently corroborated failure
  after normal startup/transition windows. Define the corroborating signals.
- **Diagnostic only** — insufficient evidence; log/measure, but do not reset.

The policy must explicitly prohibit queue depth alone, especially a zero queue,
from triggering a reset. A recovery proposal needs all of the following:

1. renderer state is stable and actively expected to consume frames;
2. it is outside a startup, reset, graph-rebuild, source/display transition,
   pause, and cooldown window;
3. sustained starvation is measured over a bounded interval, not one sample;
4. producer/conversion/delivery progress counters or timestamps show a broken
   expected relationship, not merely low queue depth;
5. no reset is already pending or in progress; and
6. retry rate limiting/one-shot generation protection prevents repeated resets
   for the same incident.

If these signals cannot distinguish normal immediate consumption from a stalled
sink, the correct initial outcome is better telemetry—not an automatic reset.

### 4. Instrument before changing behavior

Specify concise correlated log/OSD diagnostics needed to explain a no-fill
incident without per-frame log spam. Include a queue epoch/reset incident ID,
reset reason/source, renderer state, raw and converted depths/capacities,
capture/conversion/delivery progress counters and last-progress ages, event and
semaphore state where safely observable, dropped counts by cause, pending reset
state, and cooldown/suppression reason.

Rate-limit repeated reports and emit a transition log when starvation starts,
recovers, schedules a recovery, executes a recovery, or is suppressed.

## Non-goals

- Do not simply reset whenever VP queues are empty.
- Do not change queue maximums or add buffering as a blind workaround.
- Do not remove branch-specific LLDV, scene detection, shader, renderer, or
  format work merely to reduce the diff from `origin/main`.
- Do not use renderer-internal queue fullness as an assumption when VP has no
  reliable API to observe it.
- Do not implement a recovery policy before the comparison and evidence are
  recorded.

## Deliverables

1. A written queue/reset state diagram or transition table covering capture,
   conversion, delivery, normal immediate-consumption, startup, renderer reset,
   graph rebuild, and source/display transitions.
2. The main-versus-branch behavior matrix described above, with specific source
   locations and an explicit list of confirmed regressions, intentional
   divergences, and unknowns.
3. A reproducible diagnostic capture plan for a healthy fill, normal valid-zero
   queue state, renderer sink no-fill/starvation, and each reset type.
4. A recommended safe reset policy, including exact trigger signals, debounce,
   cooldown, incident/generation guard, and the path that only logs.
5. A follow-up implementation story or stories only for confirmed defects.
   Each must state whether it restores a main invariant or intentionally changes
   it with justification.

## Acceptance criteria

- The review identifies the ownership and capacity of every VP queue and does
  not conflate them with renderer-sink queues.
- The reset policy proves that an empty VP queue alone cannot trigger a reset
  loop.
- Every queue clear/reset/re-prime path has a reason, lifecycle state, and
  counter/epoch consequence documented.
- `origin/main` alignment is evaluated by behavioral invariants, not wholesale
  code reversion.
- Any recommended code change is backed by a reproducible failing scenario and
  observability sufficient to confirm the fix does not cause drops or prevent
  sink fill.
