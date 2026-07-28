# VP-0039: Alpha cadence due-forecast liveness and diagnostics

## Status

Backlog. Reported July 28, 2026 against the Alpha renderer with Scene Detect
enabled. This is future corrective work following VP-0027, not a reopening of
that completed implementation story.

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

