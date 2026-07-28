# VP-0033: Alpha renderer libplacebo frame-mixing smooth motion

## Status

Draft — low priority. Alpha refresh-rate switching normally removes the need
for this feature. It is a fallback for unavoidable source/display-rate mismatch,
not a replacement for correct refresh switching.

## User story

As an Alpha renderer user whose display cannot run at the captured content
cadence, I want an optional libplacebo frame-mixing mode that reduces cadence
judder while keeping VP stable and predictable.

## Terminology and non-goal

Libplacebo calls this frame mixing/interpolation. It blends or mixes timed
neighboring frames through `pl_queue` and `pl_render_image_mix` to reduce
judder from source/display-rate mismatch.

It is **not** optical-flow, motion-vector, or motion-compensated intermediate
frame generation. Do not market it as true motion interpolation and do not
attempt to emulate motion-compensated processors in this story.

## Scope

- Alpha/libplacebo renderer only.
- Optional and disabled by default.
- Use only when a verified actual display cadence cannot be matched through the
  existing refresh-rate selection policy, or when a user explicitly selects it.
- Define bounded latency and queue behavior suitable for VP's live capture
  path.

Do not replace Alpha's normal direct `pl_render_image` path for matched
cadence, nor weaken the existing display-rate/restart safety rules.

## Required implementation work

1. Decide whether VP's existing queue can safely feed libplacebo's `pl_queue`
   or whether a small renderer-owned timing adapter is required. Preserve
   source-buffer lifetime and the Alpha low-latency queue guarantees.
2. Feed monotonic PTS, accurate durations, and actual predicted-vsync timing.
   Explicitly handle discontinuities, renderer reset, seek-like source changes,
   display-mode changes, and missing future frames.
3. Select and document an initial mixer policy. Make any blend radius, latency,
   and mismatch thresholds explicit and bounded.
4. Require a verified mismatch before AUTO activates. A stale display target or
   a transient timing sample must not activate blending unnecessarily.
5. Provide clear OSD/log state: disabled, unavailable, matched-rate bypass,
   active mixer, source/display rates, blend/latency policy, queue health, and
   fallback reason.
6. Add configuration and HTML help only after the behavior is stable. The
   setting must make the non-motion-compensated limitation clear.

## Verification

- Test 23.976 at 59.94/60, 25 at 60, 29.97 at 60, and matched-rate controls.
- Confirm matched cadence bypasses mixing and retains the current low-latency
  behavior.
- Measure visual judder reduction, blend/ghosting artifacts, end-to-end
  latency, queue depth, dropped/repeated frames, and audio synchronization.
- Test renderer switching, refresh switches, HDR/SDR/LLDV transitions,
  display-mode changes, and source discontinuities. No reset or switching loop
  may result.

## Acceptance criteria

- VP exposes an optional, accurately named frame-mixing feature for verified
  mismatched cadence.
- It is disabled by default and never replaces normal refresh-rate matching.
- Timing, buffering, and fallback behavior are bounded and observable.
- The feature does not claim or attempt true motion-compensated interpolation.
