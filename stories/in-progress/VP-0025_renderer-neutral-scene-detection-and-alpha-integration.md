# VP-0025: Renderer-neutral scene detection and Alpha integration

## Status

In Progress. Source implementation commit `6e15206` on
`codex/vp-0025-scene-detection` extracts the proven P010 scene detector into
one renderer-neutral component and feeds both DirectShow and Alpha adapters.
The branch is based on `origin/v1.1.014-beta` in
`C:\Users\bslac\vp\videoprocessor-vp-0025`.

Alpha analyzes its converted P010 luma immediately before GPU upload and
publishes Disabled/Warming/Active/Unavailable lifecycle state without changing
queueing or presentation. DirectShow retains its existing event sequence and
counter behavior. Generation changes reset detector history; display-profile
changes alone do not.

Validation: the full `Release|x64` solution builds with zero warnings and zero
errors, and all 56 automated tests pass. Seven detector tests cover warm-up,
hard cuts, one-frame flashes, near-black de-duplication, invalid input,
generation resets, and identical results for tight and padded P010 adapters.
Live renderer lifecycle, retained-sequence comparison, and 4K cost validation
remain before Review.

Supersedes VP-0016 and the detector portion of VP-0006.

## User story

As an Alpha-renderer user, I want the established scene detector to identify
safe correction boundaries on the exact frames Alpha renders, with truthful
status and lifecycle reporting, so later cadence corrections do not require a
second competing detector.

## Scope

- Refactor renderer-independent P010 scene-signature analysis out of the
  DirectShow output pin.
- Keep DirectShow thresholds, event behavior, and correction behavior
  unchanged.
- Adapt Alpha's formatted P010 luma to the shared detector.
- Provide detection only; do not drop, repeat, prefill, or alter presentation.

## Required design and implementation

1. Define a renderer-neutral detector input containing P010 luma pointer,
   dimensions/stride, source sequence, timestamp, and reset generation.
2. Define a result containing Disabled/Warming/Active/Failed state, safe
   boundary flag, event ID, event frames-back value, average luma, and exact
   source sequence/generation association.
3. Extract the existing scene-signature, histogram, near-black, confirmation,
   and duplicate-event logic into a bounded component with no `IMediaSample`
   or DirectShow dependency.
4. Adapt the DirectShow path to the component and prove its output is unchanged
   for fixed test vectors and retained sample sequences.
5. Feed Alpha from its converted P010 frame before upload/presentation at a
   lifecycle-safe point. Scene analysis must not hold a capture source buffer,
   block queue locks, or run on a stale renderer generation.
6. Reset detector history on source discontinuity, format/dimension change,
   renderer generation change, and explicit reset. Screen-profile changes that
   do not change source pixels must not invent scene events.
7. Implement truthful Alpha OSD/interface state. `Unavailable` is reserved for
   missing capability or failure; warm-up and disabled states must remain
   distinguishable.
8. Provide the event timing needed by VP-0027. If detection happens too late to
   select the intended frame, record whether the current new-scene frame or a
   one-frame lookahead is required; do not silently add latency in this story.

## Verification

- Unit-test identical, gradual-change, hard-cut, flash, fade, and near-black
  sequences.
- Feed identical P010 sequences through DirectShow and Alpha adapters and
  compare event IDs, event frames-back, luma, and state transitions.
- Exercise SDR, HDR/LLDV-derived P010, 23.976/24, and 59.94/60.
- Exercise reset, renderer rebuild, source transition, profile transition,
  minimized/restore, and detector disable/enable.
- Measure detector cost at 4K and prove it does not cause queue growth.

## Acceptance criteria

- DirectShow scene behavior has no intentional regression.
- Alpha reports Warming then Active and associates every event with the correct
  source sequence and generation.
- One shared detector implementation serves both renderer adapters.
- Detection adds no drops, repeats, queue resets, or presentation changes.

## Dependencies and follow-ups

Coordinate sequence/generation fields with VP-0024. Unblocks VP-0027.
