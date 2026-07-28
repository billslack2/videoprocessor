# VP-0039: Alpha cadence due-forecast liveness and diagnostics

## Status

In progress. Reported July 28, 2026 against the Alpha renderer with Scene
Detect enabled. Design and implementation started July 28, 2026 from the
current `v1.1.014-beta` integration branch. This is corrective work following
VP-0027, not a reopening of that completed implementation story.

The diagnostic and liveness implementation commits merged with the joined
viewport/NLS branch through PR `#17` as `71bfb09` on 2026-07-28. The exact
merge commit passed a clean Release x64 build and all 181 native tests.
The story remains In Progress because the completed madVR hardware run does
not satisfy the Alpha-specific due-forecast/action/verification acceptance
criteria; a retained Alpha reproduction log and hardware run are still
required before Done.

The incident's matching debug log was no longer retained when this story was
written. The reproduction evidence is the user-supplied OSD screenshot and
must not be represented as log-confirmed.

## User story

As an Alpha-renderer user with scene-aware correction enabled, I want a cadence
forecast that reaches its deadline to either perform and verify one correction
or state exactly why it is being withheld, so VP never sits indefinitely at
`Repeat in 0s` or `Drop in 0s` while reporting no action.

## Reported evidence

The OSD showed:

- Alpha renderer, Scene Mode `On`
- `Status: Forecasting`
- `Forecast: Repeat in 0s`
- `Action D/R: 0 / 0`
- `Detected: 16`
- queue `0 / 1`
- refresh `59.940060 Hz`
- measured display rate `59.950599 Hz`
- estimated rate `59.941098 Hz`
- estimated mismatch `-17 ppm`

The countdown reached zero and remained there. No drop or repeat was counted.
The user did not observe an obvious correction.

## Existing contract and likely failure boundary

VP-0027 requires prediction, authorization, action, and verification to remain
separate. A due prediction is not by itself permission to alter cadence.
Failing closed can therefore be correct, but an indefinite zero countdown with
no stated blocker is not truthful or diagnosable.

The current policy clamps the predicted time to zero after accumulated phase
passes the correction deadline. It may still reject the action when
presentation evidence, queue direction, source-to-present debt, scene-event
eligibility, fallback timing, cooldown, verification, or generation state does
not authorize it. The existing OSD continues to label this state
`Forecasting` and does not expose the rejecting gate.

For a repeat, there is also a renderer integration boundary after policy
authorization: the retained frame must be pushed back onto the queue. An
enqueue rejection, generation change, cancellation, or ownership failure must
be distinguishable from a policy that never authorized the repeat.

The screenshot's queue depth of `0 / 1` appears directionally compatible with
a repeat, but it does not prove that presentation debt or the remaining gates
agreed.

## Investigation requirements

1. Reproduce at 59.94/60 with a controlled negative mismatch near the reported
   `-17 ppm`, both accelerated and at realistic accumulation speed.
2. Follow one policy generation from stable-rate publication through phase
   deadline, scene opportunity or fallback, action issue, queue execution, and
   presentation verification.
3. Determine whether the incident is caused by:
   - queue/debt disagreement that can never resolve under the present repeat
     model;
   - repeated policy-generation resets preventing fallback eligibility;
   - scene-event freshness or boundary selection;
   - cooldown or pending-verification state;
   - an authorized repeat that fails to re-enter the queue;
   - an action that executes but is not counted or verified; or
   - OSD rounding that displays zero before the action is actually due.
4. Confirm the bounded no-cut fallback advances and becomes eligible as
   designed when no suitable scene arrives.
5. Compare the policy's queue-depth sample with the queue state used by the
   renderer. Document whether `queueDepthAfterDequeue == 0` and desired depth
   `1` is the intended repeat authorization condition.
6. Confirm source-buffer ownership and generation checks cannot silently
   cancel a repeat after authorization.

## Diagnostic requirements

Add state-change logging, not per-frame spam. Each transition after a forecast
becomes actionable must make the following reconstructable:

- policy and detector generation;
- source sequence and last present ID;
- raw phase, filtered mismatch, capture/display rates, and rate evidence;
- predicted action and precise time before or past its deadline;
- queue current/desired depth, oldest queued age, and presentation debt;
- scene event ID, safety, freshness, and fallback age/eligibility;
- cooldown and pending-verification state;
- requested action, authorization result, and a stable no-action reason enum;
- renderer enqueue/consume outcome for a repeat or release outcome for a drop;
- counter update and presentation-verification result.

Log only transitions, reason changes, periodic prolonged-block summaries, and
action/verification outcomes. A prolonged due state must produce enough
evidence to identify the gate without flooding `vp_debug.log`.

## Required behavior

1. Before the deadline, show a positive forecast using enough precision that
   rounding cannot prematurely display `0s`.
2. Once due, do not indefinitely show `Repeat in 0s` or `Drop in 0s`.
   The policy must either:
   - authorize, execute, count, and verify exactly one correction; or
   - fail closed and publish the exact current blocker.
3. Represent due-but-blocked states truthfully in the OSD, for example
   `Repeat due - waiting for presentation debt`, `waiting for scene`, or
   `timing evidence unavailable`. Final wording may be shortened to fit.
4. If the bounded fallback is valid and all safety gates agree, it must provide
   liveness even when no scene boundary arrives.
5. Increment `Action D/R` only after the native action succeeds. Failed repeat
   enqueue or stale-generation cancellation must be logged and must return the
   policy to a safe, recoverable state.
6. Preserve VP-0027's fail-closed behavior. Do not force a correction merely
   because the displayed countdown reached zero.

## Design

### Reviewed current flow

The Alpha render thread removes one frame from its FIFO and snapshots
`queueDepthAfterDequeue`, desired depth, oldest queued age, and DXGI
presentation debt. `AlphaCadenceCorrectionPolicy` then predicts from filtered
capture/display mismatch, accumulates signed phase, authorizes against
queue/debt and scene/fallback gates, and reserves the correction by moving
phase back one frame and entering pending verification.

A drop returns from `RenderLocked` without presenting the selected frame; the
render loop releases it and updates the drop counters. A repeat first presents
the selected frame, then pushes that same source-owned frame back onto the
front of the FIFO with a `cadenceRepeat` marker. Only a successful push updates
the repeat counter. A failed push calls `CancelPendingAction`, which restores
the reserved phase.

The current forecast calculation clamps a passed deadline to zero. The policy
then returns `None` at each rejecting gate without publishing which gate
rejected it, so the renderer continues to publish `Forecasting` and the OSD
continues to render `Repeat in 0s` or `Drop in 0s`.

### Reviewed incident boundary

For a repeat, the current queue/debt authorization expression is:

`queueDepthAfterDequeue < desiredQueueDepth && presentationDebt == 0`.

At the reported `0 / 1`, that queue direction is correct. If the policy saw
that same depth, debt zero, a stable generation, and phase at or beyond one
frame, its repeat fallback would already be eligible and it should authorize
the repeat immediately. The screenshot therefore does not prove the policy
saw a qualifying sample. The first diagnostic implementation must distinguish
at least presentation debt, the policy's after-dequeue depth, fallback state,
generation replacement, and renderer re-enqueue cancellation.

### Policy contract

Add a stable `AlphaCadenceBlockReason` enum to every decision. Keep prediction,
authorization, execution, and verification separate:

1. A signed deadline value remains positive before the deadline and becomes
   negative after it. `due` is explicit and is not inferred from rounded OSD
   text.
2. A due decision publishes exactly one current blocker selected in policy
   evaluation order: cooldown, pending verification, queue direction,
   presentation debt, scene opportunity, or fallback queue age.
3. Precondition failures also have stable reasons for diagnostics, including
   unavailable/disjoint presentation evidence, invalid rates, incompatible
   mismatch, rate stabilization, and no actionable mismatch.
4. Authorization publishes `None`; renderer execution failures publish a
   separate native outcome and cancel the reserved policy action.
5. Policy generation replacement is observable as a transition, not confused
   with a normal forecast restart.

### Publication and logging

Publish `due`, block reason, signed deadline, policy generation, and the input
snapshot used for the decision beside the existing Alpha timing atomics. The
OSD will show a positive countdown before due and, once due, replace the zero
countdown with `Repeat due - <reason>` or `Drop due - <reason>`.

The render thread owns a small diagnostic state machine. It logs on generation
change, first due transition, block-reason change, authorization, native
success/failure, and verification completion. While one due reason remains
unchanged it emits a bounded periodic summary, never a per-frame message. Each
record includes both generations, source sequence, present ID, phase,
mismatch/rates, queue/debt, scene/fallback state, and verification state.

### Execution ordering

Keep the current fail-closed reservation model, with these explicit outcomes:

- drop succeeds only when the selected source reference is released as the
  intentional correction;
- repeat succeeds only after `push_front` accepts the exact retained frame
  under the same queue generation;
- stop or generation rejection is logged distinctly from allocation/queue
  failure;
- any failed repeat enqueue restores policy phase exactly once and releases
  the source reference exactly once;
- D/R counters change only after those native success points.

### Implementation slices

1. Policy: signed deadline, explicit due state and stable reason enum, with
   gate-isolation unit tests at/beyond the deadline.
2. Publication: Alpha OSD due wording and transition-only policy diagnostics.
3. Native outcomes: repeat enqueue/cancellation result enum, exact ownership
   tests, and action/verification correlation.
4. Sustained validation: accelerated mismatch harness followed by a real
   59.94/60 run across multiple deadlines.

## Design review

Reviewed July 28, 2026 against `v1.1.014-beta`.

- The design preserves VP-0027's separation of prediction and authorization;
  an OSD deadline never bypasses a safety gate.
- The stable enum belongs in the policy rather than being reconstructed from
  renderer symptoms, which keeps unit tests deterministic and log wording
  forward-compatible.
- Signed deadline publication fixes both premature/indefinite zero display and
  supplies the requested before/past-deadline precision.
- Renderer outcome remains separate because a policy authorization cannot
  truthfully describe a later queue-generation rejection or enqueue failure.
- DirectShow interfaces and behavior remain unchanged; new publication is
  Alpha-specific behind optional renderer diagnostics.
- The fallback is intentionally bounded by policy frames, not wall-clock OSD
  rounding. Liveness remains conditional on queue/debt and evidence gates.

Review result: approved for implementation in the four slices above. Slice 1
is the initial implementation target.

## Implementation progress

Started July 28, 2026 on `codex/vp-0039` from the verified GitHub default
branch `v1.1.014-beta`.

Commit `6cb7a1a` (`Publish Alpha cadence due blockers`) implements slice 1 and
the OSD portion of slice 2:

- adds a stable policy block-reason enum and explicit due state;
- preserves the signed deadline after it passes instead of clamping it to
  zero;
- separates queue-direction and presentation-debt blockers for drop and
  repeat;
- distinguishes fallback maturity and drop queue-age gating;
- publishes one coherent Alpha due/action/reason snapshot through the renderer
  and plugin boundary;
- changes the OSD from an indefinite zero countdown to
  `<action> due - <reason>`;
- adds four due-state tests covering signed overdue time, repeat queue versus
  debt, drop queue/debt/fallback age, and no-scene repeat fallback.

Validation on the implementation commit:

- Debug x64 `VideoProcessor-GUI` build: passed;
- Debug x64 `VideoProcessor-Test` build: passed;
- all 14 `AlphaCadenceCorrectionPolicyTests`: passed;
- focused four-test due-state filter after final publication changes: passed.

Commit `22c7157` (`Trace Alpha cadence correction lifecycle`) completes the
manual-test diagnostic slice:

- logs policy-generation changes, first due transition, block-reason changes,
  due resolution/authorization, and one prolonged summary every 30 seconds;
- records policy, detector, presentation, and queue generations; source and
  present IDs; signed raw/filtered mismatch; rates and evidence samples;
  phase/deadline; queue depth/age/debt; scene freshness; fallback maturity;
  cooldown; and pending verification;
- assigns one `action_id` across authorization, native execution, queue
  cancellation, and presentation verification;
- distinguishes repeat enqueue, stop rejection, generation rejection,
  exception, second-render consumption, second-render failure, and queued
  repeat cancellation;
- rolls policy phase back after failed enqueue/render/consume and logs source
  ownership disposition;
- preserves an authorized repeat under queue pressure by dropping the incoming
  frame instead of the retained repeat;
- increments the repeat counter only after the retained frame's second render
  succeeds, not merely when it is re-enqueued;
- stores the real signed seconds from deadline for successful drop/repeat OSD
  confirmation.

Validation after the logging and native-outcome changes:

- Debug x64 `VideoProcessor-Libplacebo` build: passed;
- Debug x64 `VideoProcessor-GUI` build: passed;
- Debug x64 `VideoProcessor-Test` build: passed;
- all 14 `AlphaCadenceCorrectionPolicyTests`: passed.

The implementation is ready for the sustained accelerated and real 59.94/60
manual sessions. Remaining work is manual evidence capture, any
incident-specific correction revealed by that evidence, and dedicated
fault-injection/ownership tests for renderer-native failure branches. The
story remains in progress.

Commit `a8c5b90` (`Simplify Alpha timing OSD language`) separates end-user
wording from technical diagnostics. The OSD now uses short phrases such as
`Waiting for timing`, `Measuring timing`, and `Timing matched`. Internal
reason enums and detailed log records remain unchanged.

Commit `ea4b97c` (`Compact Alpha timing OSD`) applies the final limited-width
OSD treatment:

- uses a 20 px Alpha-only font and line height while leaving the 23 px legacy
  overlay unchanged;
- shortens Alpha state values to `Waiting`, `Measuring`, `Matched`,
  `Forecasting`, `Checking`, `Unavailable`, or `Due`;
- changes the long forecast sentence to compact rows such as
  `Next: R in 12s`, `Next: R due: timing`, and `Last: R on time`;
- shortens the remaining scene labels and end-user blocker descriptions while
  retaining exact policy enum names and full snapshots in technical logs.

Debug x64 Alpha plugin and GUI builds passed. A 20 px Consolas width check put
the longest new timing example (`Next: R due: buffer wait`) at approximately
370 px, within the 420 px overlay with padding and margin remaining.

Manual-test deployment on July 28, 2026:

- clean x64 Release GUI and Alpha plugin rebuilds passed from source commit
  `3f50429` with `VERSION_DIRTY=false`;
- deployed the executable, Alpha plugin, and matching symbols to
  `C:\Videoprocessor\vp`;
- backed up the replaced binaries under
  `C:\Videoprocessor\vp\backup-before-vp0039-20260728-110511`;
- verified every deployed SHA-256 hash against its Release artifact;
- left `VideoProcessor.cfg`, state, shaders, and runtime dependencies
  unchanged.

## Verification

- Unit-test both drop and repeat paths before, exactly at, and beyond the phase
  deadline.
- Block each authorization gate individually and assert a distinct published
  reason plus truthful OSD state.
- Test no-scene fallback, frequent scene events, stale event IDs, cooldown,
  pending verification, invalid/disjoint presentation evidence, and policy
  generation replacement.
- Test successful and failed repeat enqueue, ensuring exact source-reference
  ownership and no repeat loop.
- Verify one successful action changes only the matching D/R counter and
  receives one presentation-verification result.
- Run a sustained real 59.94/60 session and accelerated controlled-mismatch
  sessions long enough to cross multiple correction deadlines.
- Confirm queue health, latency, dropped-frame accounting, stop/restart,
  renderer switching, mode changes, and DirectShow behavior do not regress.

## Acceptance criteria

- A due Alpha cadence forecast cannot remain at zero indefinitely without an
  explicit, logged reason.
- When all safety gates agree, exactly one correction occurs within the scene
  window or bounded fallback and is verified.
- When a safety gate does not agree, no correction occurs and both OSD and logs
  identify that gate accurately.
- No hidden policy reset, failed repeat enqueue, counter mismatch, stale frame,
  ownership error, reset loop, queue starvation, or unexplained dropped-frame
  increase remains in the exercised paths.
- DirectShow renderer behavior is unchanged.

## Dependencies

Depends on the accepted Alpha telemetry, detector, queue, and correction work
in VP-0024 through VP-0027.
