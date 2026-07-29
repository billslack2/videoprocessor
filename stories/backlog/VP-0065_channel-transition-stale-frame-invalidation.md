# VP-0065: Invalidate stale frames during channel and stream transitions

## Status

Backlog. This story captures a recurring user-visible defect that remains
unresolved and requires instrumentation before implementation can be safely
chosen.

## User story

As a viewer changing channels in YouTube TV or another live source, I want the
old channel's picture to disappear immediately, so a previously presented
frame cannot flash during the new channel's stream or renderer transition.

## Observed behavior

Changing channels often used to trigger a full restart or HDMI resynchronizing
transition. That behavior has improved and the transition now frequently
continues without a renderer restart, but an old frame can flash while the new
channel is being established. The frame can appear unrelated to the current
content and may be from an earlier Alpha-renderer or external-renderer
presentation. A black screen during this short transition is acceptable; a
stale picture is not.

The exact source is not yet proven. The candidate sources are an old frame in
VP's internal queue, a renderer queue or retained presentation surface, an
Alpha host surface, or a Windows compositor surface exposed while the live
source is rebuilding. The issue must not be assumed to be a scene-detection
problem.

## Goals

1. Detect a live-source/channel or stream-generation transition as early as
   the source and graph state make it observable, before an old frame can be
   presented as new content.
2. Associate queued and presented frames with a source/stream generation.
3. Invalidate or discard frames belonging to the previous generation before
   normal presentation resumes.
4. Ensure a renderer that cannot synchronously clear its retained surface is
   covered by a VP-owned black transition surface until the first valid frame
   from the new generation has been presented.
5. Do not require a renderer restart or HDMI resync for ordinary channel
   changes.

## Investigation and design requirements

- Trace the YouTube TV/live-source channel-change path through capture,
  DirectShow delivery, VP queues, renderer handoff, and presentation.
- Identify the earliest reliable transition signal: source format/metadata
  change, discontinuity, timestamp discontinuity, graph flush, pin reconnect,
  capture-generation change, or another source event.
- Add bounded diagnostic logging for transition generation, frame generation,
  enqueue/dequeue/present events, renderer ownership, queue invalidation, and
  black-cover activation/removal. Logging must make it possible to determine
  whether the stale frame came from VP, Alpha, the external renderer, or the
  compositor.
- Confirm whether a renderer-side flush/reset is safe and whether it must be
  paired with VP queue invalidation. Do not blindly reset on a low queue count;
  zero or one queued item can be valid during normal playback.
- Define what constitutes the first valid new-generation frame. It must be
  accepted only after the new source generation is known and the frame has
  passed normal format/metadata validation.
- Keep old and new frames from being mixed during a transition, including
  transitions that do not restart the renderer.

## Candidate implementation

Prefer a source-generation barrier over a renderer restart:

1. On a confirmed source/stream transition, increment the generation and stop
   old-generation frames from entering presentation.
2. Atomically clear or invalidate VP-owned queues using the generation rather
   than relying only on a queue reset call.
3. Request the renderer's supported flush/clear operation when available.
4. If the visible renderer surface can retain an old frame, activate a
   full-picture black cover at the same z-order as the video surface.
5. Remove the cover only after a valid frame from the new generation has been
   presented.

The black cover must cover the complete visible video area, including any
letterbox or pillarbox region, and must not permanently obscure the OSD or
normal video. Its lifetime and generation must be logged.

## Acceptance criteria

- Repeated live-channel changes do not display a frame from an earlier
  channel, stream generation, or renderer session.
- The transition works when no renderer restart or HDMI resynchronization
  occurs.
- Old-generation frames cannot be presented after the transition barrier.
- If the renderer retains an old surface, the viewer sees black until the
  first valid new-generation frame instead of the stale frame.
- Normal playback is not repeatedly reset when queues are legitimately empty
  or shallow.
- Alpha and external-renderer paths are covered, or their unsupported clear
  behavior is explicitly documented and safely covered by VP.
- Logs clearly identify the transition cause, generation, discarded frames,
  renderer action, and first accepted new-generation frame.
- Regression tests cover a channel change with queued old frames, a channel
  change with empty queues, a format/metadata change without renderer restart,
  and a renderer handoff during the transition.

## Dependencies and validation

- Review the current source/pin/graph transition signals and existing queue
  reset and stale-frame protections before implementation.
- Reuse the existing renderer lifecycle and queue ownership contracts where
  safe; do not make renderer restart the required solution.
- Validate with repeated YouTube TV channel changes and a controlled source
  that can reproduce a stream-generation change without HDMI resync.
- Capture `C:\Videoprocessor\vp\logs\vp_debug.log` with the bounded
  transition diagnostics enabled for any unresolved reproduction.
