# VP-0145: Renderer post-stall reset eligibility

## Status

Backlog (2026-08-23). This root is decomposed so the first implementation is
telemetry and diagnostics only. It must not initiate, coalesce, delay, or
otherwise change a renderer reset.

## User story

As a VideoProcessor operator, I want clear, renderer-neutral evidence that a
post-stall recovery has failed to settle, so I can distinguish a transient
settings/NLS disruption from a condition where resetting the renderer is
likely to restore queue and latency health.

## Incident evidence

- In the 2026-08-22 22:45 to 2026-08-23 00:32 session, the VP Renderer
  experienced NLS-related stalls of up to 4.24 seconds. Automatic backlog
  trimming ran, but the renderer remained at queue depth 4 (healthy range
  2..3), oldest-frame age about 52 ms, and swap time about 10--11 ms for
  roughly 40 seconds. The manual reset at 22:50:08 restored queue depth 2,
  oldest-frame age about 16.7 ms, and swap time about 0.08 ms.
- The madVR/DirectShow path had source-gap and output-readiness graph resets.
  Its VP-side queue recovered to 2 frames, but the 22:59 sequence retained an
  approximately one-frame increase in scheduled PTS lead. madVR occupancy is
  currently unobservable, so a latency step alone is not proof that another
  reset would help.

## Required behavior

1. Establish shared post-stall recovery diagnostics for both VP Renderer and
   DirectShow - madVR without changing runtime recovery behavior.
2. Classify the state as healthy, settling, reset-advisory, or suppressed with
   structured reasons and the applicable renderer/generation/configuration
   identity.
3. A persistent reset-advisory must log `reset should occur` immediately on
   transition and again at most once every 10 seconds while it remains true.
   Returning to healthy or entering a suppression window must also be logged.
4. Initial thresholds are diagnostic hypotheses, not automatic policy:
   following a material stall, the renderer must be quiet from settings,
   shader, refresh, and lifecycle changes; output timing must be valid; and
   evidence must persist across multiple observations. VP Renderer evidence
   includes queue health, oldest age in display frames, and present/swap
   timing. madVR evidence includes DirectShow pipeline queues, normalized PTS
   lead/latency at the same rate and configuration, source/delivery health,
   madVR configuration, and explicit `occupancy=unobservable` when applicable.
5. Reset advice must be suppressed during renderer construction/retirement,
   output readiness rewarm, active graph reset, and a short quiet window after
   a settings, shader, profile, or refresh transition. The diagnostic must
   state the suppression reason.
6. This root does not authorize automatic reset. Any later action policy must
   be separately reviewed against the telemetry collected by child task 1.

## Decomposition

1. [VP-0145-1](VP-0145-1_post-stall-reset-telemetry-and-diagnostics.md) --
   Add cross-renderer telemetry and a non-mutating `reset should occur`
   advisory. It is independently testable from logs and has no reset side
   effects.
2. A subsequent child task may calibrate the advisory thresholds and decide
   whether to expose an operator action or a controlled automatic reset. Do
   not create or begin that task until the VP-0145-1 evidence is reviewed.

The root may close after VP-0145-1 is accepted and the collected evidence is
recorded with a deliberate decision on the next remediation step. It does not
require automatic reset implementation.

## Acceptance criteria

1. VP-0145-1 produces enough structured evidence to reproduce the distinction
   between the successful 22:50 VP reset case and the ambiguous madVR
   post-reset latency-step case.
2. No renderer reset, queue trim, timing rebase, presentation setting, or
   graph lifecycle behavior changes under this root's first child.
3. Subsequent work has an evidence-backed decision rather than treating any
   single stall, queue increase, or latency increase as reset-worthy.

## Related work

- VP-0046, DirectShow event plumbing and passive health diagnostics
- VP-0084, Bound DirectShow total steady queue after reset
- VP-0130, Renderer telemetry and live input-configuration clarity
- VP-0137, Restore bounded madVR queue, NLS, and renderer-switch behavior

## Tracker audit note

- Audit against fetched `origin/main` on 2026-08-23 found 159 canonical story
  files and table entries, no duplicate IDs, and highest root ID `VP-0144`.
  `VP-0145` is therefore the next valid root ID.
