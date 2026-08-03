# VP-0081: Preserve madVR NLS geometry through output-readiness re-primes

## Status

Backlog. This is a DirectShow/madVR regression discovered on the deployed
`v1.1.015-beta` build (`8b8d900`) on 2026-08-02. No implementation branch has
been chosen.

## User story

As a CIH-scope user using NLS through madVR, I want a valid active-picture
rectangle to survive VP's automatic output-readiness graph re-prime when the
source and viewport have not changed, so a stable 2.20:1 movie does not fall
back to `Waiting` after NLS has already engaged correctly.

## Incident evidence

The deployed log is `C:\Videoprocessor\vp\logs\vp_debug.log`. During HDR
23.976 content on the `scope` viewport (target aspect 2.3500), the DirectShow
detector and NLS initially worked correctly:

```text
23:50:13 | ACTIVE PICTURE: publication generation=1 state=stable
            aspect=2.2018 bounds=0,208-3840,1952 raster=3840x2160
23:50:13 | Shaders: NLS mapping=active ... source=2.2018 target=2.3500
            axis=horizontal stretch=1.06729
```

Six seconds later, VP's normal post-ready lifecycle ran a graph reset:

```text
23:50:19 | Output readiness graph re-prime request: ... accepted=1
23:50:19 | Reset started: ... reason=output-readiness scope=graph
23:50:19 | CBufferedLiveSourceVideoOutputPin::Reset() - HDMI resync async
            queue reset starting
23:50:19 | Shaders: NLS mapping ... mapping=waiting ... last_safe=active
            reason="transition geometry is not stable; safe passthrough"
```

`CBufferedLiveSourceVideoOutputPin::Reset()` clears the active-picture
rectangle/analyzer state. The next observations were intermittently
provisional/ambiguous, so the same 2.20:1 picture did not promptly reacquire.
The Alpha renderer has an independent active-picture path and did not show the
same failure.

## Problem statement

An output-readiness re-prime is a VP graph/timing recovery boundary, not proof
that the source picture's aspect changed. Clearing the only trusted rectangle
at that boundary unnecessarily removes NLS from a stable picture. Conversely,
blindly retaining a rectangle across every reset would be unsafe: a real
channel/input change, resolution change, viewport change, or renderer change
could temporarily apply an old crop/stretch to new content.

The implementation must distinguish the narrowly safe *same-source graph
re-prime* from those real content/presentation boundaries.

## Scope

1. Trace the DirectShow reset call path from `OutputReadiness` through
   `DirectShowVideoRenderer::Reset()` and
   `CBufferedLiveSourceVideoOutputPin::Reset()` to identify the smallest
   explicit handoff point for an output-readiness preservation token.
2. Preserve the last *trusted, stable* active rectangle only when all of the
   following remain true:
   - reset reason is `OutputReadiness`;
   - renderer instance/generation remains DirectShow/madVR;
   - delivered raster, input encoding/format contract, and effective VP
     viewport/screen-profile generation are unchanged; and
   - no source discontinuity or input/format/refresh-family transition has
     invalidated the token.
3. During post-reset detector reacquisition, let NLS keep using that retained
   rectangle. The retained state is provisional only for lifecycle purposes;
   it must never be published as a newly measured geometry or treated as
   confirmation of a new aspect.
4. Replace the retained rectangle immediately when the analyzer confirms a
   new trusted rectangle. Withdraw it safely when a real invalidating boundary
   occurs or when the detector confirms a materially different geometry.
5. Add concise logs for: preservation eligibility/result, the retained
   rectangle/generation, reuse after reset, normal reacquisition, invalidation
   reason, and NLS mapping changes. Do not emit per-frame logs.
6. Add focused unit tests around the transition/state model or an extracted
   reset-preservation helper. Cover a preserved same-source re-prime, explicit
   invalidation, and replacement by a confirmed different aspect.

## Non-goals

- Do not weaken crop authority or accept arbitrary one-edge/provisional black
  bar observations as stable geometry.
- Do not preserve geometry across a renderer swap, explicit renderer restart,
  viewport hotkey, input resolution/format change, or confirmed content
  transition.
- Do not make NLS stretch while geometry is genuinely unknown at initial
  startup.
- Do not change Alpha's independent geometry pipeline in this story except
  for shared tests/helpers that are demonstrably renderer-neutral.
- Do not reintroduce a renderer restart for ordinary mixed-aspect transitions.

## Validation matrix

- madVR, scope profile, HDR 23.976, stable 2.20:1 source: NLS remains active
  through the automatic output-readiness re-prime and retains the same
  `2.2018 -> 2.3500` mapping.
- madVR, scope profile, mixed 2.35/1.85 content: no stale stretch remains
  after a confirmed aspect change; only the safe bounded hold is permitted.
- Renderer swap Alpha -> madVR and madVR -> Alpha: no geometry crosses
  renderer generations.
- F2/F3 viewport changes, HDR/SDR changes, and capture resolution/format
  changes: retained geometry is invalidated before NLS is evaluated.
- Explicit renderer restart and actual HDMI resync: NLS waits for new trusted
  geometry unless the reset is specifically proven to be same-source
  output-readiness recovery.
- Build x64 Release and run the relevant unit tests. Capture one before/after
  deployed log excerpt proving both the re-prime and correct NLS result.

## Acceptance criteria

- A previously active NLS mapping for stable 2.20:1 content does not become
  permanently `Waiting` solely because of VP's output-readiness re-prime.
- The retained rectangle is only usable inside the documented same-source
  safety boundary and cannot cross an invalidating transition.
- A confirmed new geometry supersedes the retained geometry without a renderer
  restart or false-positive stretch.
- Logs make it possible to distinguish preserved geometry, detector
  reacquisition, and a deliberate safety invalidation.
- Existing VP-0034/VP-0035 mixed-aspect behavior remains covered by validation
  rather than being weakened to solve this case.

## Related stories

- VP-0034: Restart-free mixed-aspect NLS.
- VP-0035: Robust low-latency active-aspect transitions.
- VP-0066: Live-output lifecycle and deterministic re-prime.
- VP-0077: VP-0066 merged-beta acceptance validation.
- VP-0078: Alpha output-refresh transition re-prime; related lifecycle
  semantics but a separate renderer path.

