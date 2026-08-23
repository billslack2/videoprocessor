# VP-0138: Preserve Clock-Smart2 real-time timestamps

## Status

In Progress (updated 2026-08-20). A focused implementation is complete in the clean
worktree
`C:\Videoprocessor\vp\git-main\stories\.vp-vp0138-timestamps` on branch
`codex/vp-0138-smart2-realtime-timestamps`, based on the discovered default
integration branch `v1.2.001-beta` at `f572a61`. Automated Release verification
passes. The matched x64 Release host and VP Renderer DLL are deployed for live
testing; source check-in and live madVR acceptance remain pending.

## User story

As a live-HDMI madVR operator using Clock-Smart2 with separately clocked audio,
I want startup queue convergence and later source gaps to preserve real-time
video timing without cumulative or continuously regenerated timestamp
corrections, so VP returns to its preferred queue depth without permanent A/V
latency drift.

## Reported failure

A tester running source SHA `9309dc5` saw scheduled PTS lead fall in exact
one- and two-frame steps after source discontinuities while VP's own queue
remained `0/2/2`. The attached session showed approximately 41.7 ms and
82.2 ms losses at 23.976 fps. The tester configuration had scene-aware timing
correction enabled in `UPSTREAM_SAMPLE` mode even though that SHA's checked-in
configuration defaults scene detection off.

## Root causes

1. Scene-aware cadence, introduced after `origin/main`, replaced every
   non-Rational sample timestamp with a display-slot timestamp derived from
   the successfully delivered-sample count. A lost HDMI picture therefore
   compressed the Smart2 timeline by exactly one frame. In the tester log,
   240 source-frame advances with only 239 deliveries produced exactly 239
   presentation slots; a two-frame source gap produced 238 slots.
2. `LiveClockGapPreserver`, also absent from `origin/main`, could add a
   source-counter-derived cumulative synthetic offset to Smart2 hardware time.
   Its corrections in this session were only tens of microseconds and did not
   cause the frame-sized jumps, but the behavior conflicts with Smart2's
   hardware-clock authority.
3. Startup convergence joined retained converted samples to the prior
   delivered stop. With variable HDMI handshake time, that can preserve stale
   pre-Run latency instead of returning the retained live reservoir to current
   graph time and the configured presentation lead.

`DirectShowTimingClock`, PPM handling, stop-time late binding, and integer
conversion were ruled out. Smart2 does not apply PPM, the DeckLink 1 MHz to
DirectShow 10 MHz conversion is exact multiplication by ten, and late binding
changes only the stop time.

## Implemented repair

- Hardware-clock modes cannot enter scene-aware display-slot timestamp
  ownership. Scene/crop analysis may remain enabled, but timing correction is
  explicitly logged as bypassed and no planned drop, repeat, deferred clone,
  or per-sample scene timestamp can run in that mode.
- Clock-Smart2 now uses raw DeckLink hardware time without the cumulative
  `LiveClockGapPreserver` repair.
- Intentional startup convergence performs a bounded phase correction only:
  the first retained converted frame is anchored once to the later of the last
  delivered stop and confirmed running graph time plus configured lead.
- The deferred raw-queue trim from that same convergence transaction is
  compressed once when its source-counter boundary reaches delivery.
- After that transaction, genuine hardware-time gaps remain intact and are
  marked as DirectShow discontinuities. The constant epoch offset is never
  steered and resets on a new queue epoch.
- If graph time is unavailable at convergence, the rebase falls back to the
  last successfully delivered stop rather than manufacturing a clock.

## Acceptance criteria

- With Clock-Smart2 and scene correction requested, final presentation-start
  deltas continue to follow DeckLink hardware time; scene cadence does not
  replace timestamps or schedule a correction sample.
- A one- or two-frame HDMI/source discontinuity after startup cannot reduce
  PTS lead by the missing delivered-frame count.
- Startup convergence discards stale raw/converted work, returns to current
  graph time plus configured lead, and preserves the configured VP queue
  target across variable HDMI handshake duration.
- Both intentional stale spans from one convergence transaction are consumed
  without converting later genuine source gaps into continuous timestamps.
- No PPM adjustment, PLL, rolling timestamp correction, or remainder-based
  synthetic clock is introduced for Smart2.
- A queue epoch change clears the startup phase correction; the first retained
  sample and all source gaps are marked discontinuous as appropriate.
- The complete x64 Release solution builds and the native test suite passes.

## Validation evidence

- Source/current/default/main comparison identified regression commits
  `131c204` (scene cadence timestamp owner), `a338dd9` (live startup catch-up),
  and `8649322` (hardware-clock gap repair).
- Focused x64 Release test target built successfully.
- Native MSTest suite passed 867/867 after the final timing correction.
- A serialized clean x64 Release solution rebuild completed successfully with
  the current Visual Studio toolchain. Existing compiler and Qt deployment
  warnings remain; no new build error was introduced.
- `git diff --check` passes.
- Test deployment completed on 2026-08-20 with a matched Release pair:
  host SHA-256
  `51EB50D8C93BDF3B8FB20ADAB0D165E46D79C7E1432762226D956BAA26838597`
  and VP Renderer SHA-256
  `0D40A7CCF162DC4E75BB4F25074BCC42424DA3EF1A37AC6D95EA3BBDFCFD4B75`.
  The prior pair is recoverable at
  `C:\Videoprocessor\vp\backups\20260820-021601-pre-vp0138-smart2`.
- `VideoProcessor.cfg` remained byte-identical with SHA-256
  `1C68C551C4480E696D92185D170CEF854B8643FC951EC9AD251C3025F6E2BE76`.

## Remaining live validation

- Repeat immediate-through-delayed HDMI handshake cases and confirm convergence
  to the configured VP queue target and stable audio sync.
- With Clock-Smart2, request scene correction and reproduce genuine one- and
  two-frame source gaps; verify the bypass log and stable PTS lead.
- Repeat same-rate Apple TV menu to YouTube TV transitions and rapid renderer
  switches to ensure VP-0137 behavior remains intact.

## Relationship to other work

- VP-0137 fixed the bounded queue, NLS, and renderer-switch deadlocks on the
  same beta base. This story preserves those queue mechanics while correcting
  timestamp ownership.
- VP-0066-9 owns one-time live queue convergence. This story narrows its
  hardware-clock phase behavior without reverting the refactored buffering
  pipeline to `origin/main`.
- VP-0027's scene-safe cadence work remains appropriate for synthetic/display-
  owned timelines, but cannot own timestamps in Clock-Smart2.
