# VP-0057: Re-prime Alpha when it exceeds the configured queue limit

## Status

Review — deployed July 29, 2026.

The Alpha renderer previously maintained hidden burst headroom: with the
user-configured queue size set to `3`, it permitted an internal hard capacity
of `6`. This produced visible states such as `5/3`, even though the UI
presents the configured value as the queue limit.

The deployed debug log confirmed that manual reset was received and completed:
each request armed a new Alpha queue generation, flushed renderer state, and
then correctly re-primed to the configured depth. It did not leave the queue
empty because live capture immediately refills it.

## Implemented change

Alpha now treats the configured queue size as its actual hard capacity. A
queue size of `3` can no longer grow to `5`; the existing overflow behavior
drops an incoming/oldest eligible frame rather than accumulating latency.

The first deployed guard requested a queue-only reset whenever the displayed
limit was exceeded. Live validation correctly showed that it reset and
re-primed, but also exposed an undesirable loop at queue size `1`: normal
headroom appeared as `2/1`, so the queue reset every second. The final fix
removes hidden headroom and retains the guard only as protection against a
future invariant violation. No renderer graph rebuild is performed by that
guard, and madVR/DirectShow recovery behavior is unchanged.

The change is in:

- `src/VideoProcessor-GUI/VideoProcessorDlg.cpp`

## Build and deployment evidence

- Built successfully from `C:\Users\bslac\vp\videoprocessor-v1.1.015-beta`
  using x64 Release on July 29, 2026; all 209 native tests passed.
- Deployed the rebuilt `VideoProcessor.exe` and
  `vprenderer\VideoProcessorVPRenderer.dll` to `C:\Videoprocessor\vp`.
- Active configuration and all renderer runtime files were preserved.
- Rollback files:
  `C:\Videoprocessor\vp\VideoProcessor.exe.pre-alpha-hard-cap-20260729-004100`
  and
  `C:\Videoprocessor\vp\vprenderer\VideoProcessorVPRenderer.dll.pre-alpha-hard-cap-20260729-004100`.

## User story

As an Alpha-renderer user, I want the queue size I configure to be a true
upper bound, so Alpha cannot accumulate more queued frames or latency than the
UI promises.

## Acceptance criteria

- With Alpha selected and queue size `3`, Alpha never reports more than `3`
  queued frames.
- With queue size `1`, Alpha never reports more than `1` queued frame and
  does not enter a periodic reset/re-prime loop.
- An unexpected queue invariant violation is still guarded by a queue-only
  reset; it does not rebuild the renderer graph.
- madVR behavior is unchanged: its DirectShow liveness criteria remain the
  only automatic recovery trigger.
- With Auto disabled, queue depth alone does not trigger this new recovery.

## Review requested

Perform a short live Alpha test with queue sizes `1` and `3`, including rapid
renderer changes. Confirm that the current or rotated
`C:\Videoprocessor\vp\logs\vp_debug.log*` files never report a depth above
the configured size and that playback remains stable without repeated queue
re-primes.
