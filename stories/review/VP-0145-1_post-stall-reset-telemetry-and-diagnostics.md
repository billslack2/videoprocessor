# VP-0145-1: Post-stall reset telemetry and diagnostics

## Status

Review (2026-08-23). Parent:
[VP-0145](../in-progress/VP-0145_renderer-post-stall-reset-eligibility.md).
Implemented at `04d2d9a` on `codex/vp-0145-reset-advisory` (rebased onto
`v1.3.001-beta`); x64 Release
builds and 21 focused tests pass. Dedicated VP and madVR runtime log captures
remain for acceptance. The post-stall evaluator is telemetry-only.

## User story

As a VideoProcessor operator, I want a concise, repeated diagnostic that says
why a renderer has or has not recovered after a material stall, so I can see
when a reset should occur without the application performing one.

## Scope

Implement a shared recovery-health evaluator and structured diagnostic output
for VP Renderer and DirectShow - madVR. The evaluator may observe existing
state, but it must not submit a reset request or alter queue, delivery,
presentation, shader, display, or graph behavior. Separately, fullscreen
entry and renderer switches schedule the existing reset coordinator with the
configured queue-reset delay. Those deterministic resets are logged as
detected, armed or covered, and resolved or failed; advisory evaluation stays
suppressed until they have settled.

### Required telemetry

Each diagnostic observation must identify renderer, renderer/queue/graph
generation, rate/period, output-readiness state, rewarm state, configuration
or transition quiet age, last material-stall age and severity, and any reset
cooldown/suppression reason.

- **VP Renderer:** queue current/target/healthy band and oldest age in both ms
  and frames; recent range/dequeues; source, presented, and present identifiers;
  presentation debt; render and swap time (with an interval max or equivalent
  recent severity); backlog-recovery count and dropped frames.
- **DirectShow - madVR:** raw, converted, delivery, and target/reserve queue
  state; source gaps, late-bind/delivery failures, and downstream health;
  VP-internal latency, PTS lead, scheduled latency, and each normalized to
  display frames; baseline identity and delta; madVR estimated pipeline
  frames, reported OSD latency when available, and an explicit occupancy-known
  or `occupancy=unobservable` status.

### Advisory state

1. Evaluate health on the normal telemetry path and record state transitions
   immediately. While the state is settling, suppressed, or reset-advisory,
   emit an updated full observation at least every 10 seconds; avoid adding
   recurring noise while healthy.
2. Use a preliminary VP Renderer advisory only after a material stall and a
   quiet, timing-valid post-recovery window. Candidate evidence is sustained
   queue depth above the healthy band, oldest-frame age above about 1.5 display
   frames, or swap time above about half a display frame. Log the individual
   predicates and their observed durations rather than hiding them behind a
   single score.
3. For madVR, use the same quiet/validity gates and report a sustained
   normalized PTS-lead or scheduled-latency step of at least one display frame
   against a same-renderer, same-rate, same-configuration baseline. Because
   madVR occupancy is not observed, label the result as advisory evidence, not
   proof that resetting will fix it.
4. In either renderer, log exactly `reset should occur` when the advisory
   first becomes true, including all positive predicates, reference baseline,
   and suppression/cooldown status. Repeat this report no more often than once
   per 10 seconds until recovery, a lifecycle transition, or changed
   configuration resets the evidence window.
5. Log the corresponding recovery or suppression transition, including the
   duration of the advisory and why it cleared.

## Acceptance criteria

1. A VP Renderer test or controlled replay that reproduces a material stall
   followed by failed re-settlement logs the structured advisory with queue,
   oldest-frame, and swap predicates, then logs recovery if the state clears.
2. A transient disturbance that settles during the quiet/recovery window does
   not produce `reset should occur`.
3. A madVR run logs the DirectShow and madVR-specific fields, including an
   explicit unobservable occupancy status where no occupancy API is available.
4. A sustained madVR timing step can be identified relative to a compatible
   baseline and is labelled advisory rather than treated as a reset command.
5. Persistent advisory state produces a refreshed report at the 10-second
   cadence, with no faster recurring advisory spam.
6. Focused tests or deterministic log-based validation verify that the
   advisory evaluator does not call the reset coordinator, restart a renderer,
   flush a queue, or change delivery/presentation behavior. Fullscreen entry
   and renderer switching are the explicitly requested exception: they must
   use the existing delayed coordinator and log their outcome.

## Boundaries

- Do not add an automatic reset based on advisory telemetry, new hotkey
  behavior, or queue policy change.
- Do not infer madVR queue occupancy from VP-side queues or OSD latency.
- Do not compare milliseconds across refresh rates without also reporting
  display-frame normalization.
- Do not treat lifecycle startup, output-readiness rewarm, active reset, or
  active settings/NLS/refresh changes as reset advice.

## Likely implementation areas

- VP Renderer presentation telemetry and backlog-recovery paths
- DirectShow live-source latency, queue, source-gap, and delivery telemetry
- madVR runtime/configuration telemetry integration
- Renderer lifecycle/reset-coordinator diagnostic state
- Focused telemetry/recovery tests or deterministic log assertions

## Validation evidence to record on completion

- VP Renderer post-NLS-stall run demonstrating either a recovered state or a
  persistent advisory using the required fields.
- madVR source-gap or timing-step run demonstrating compatible-baseline and
  `occupancy=unobservable` reporting.
- Focused test/log evidence that no reset request was emitted by the advisory,
  plus VP and madVR logs showing deterministic fullscreen/switch resets before
  post-stall telemetry evaluates recovery.

## Tracker audit note

- Created with parent `VP-0145` after the 2026-08-23 canonical ID audit.
