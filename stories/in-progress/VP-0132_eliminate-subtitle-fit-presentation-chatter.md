# VP-0132: Eliminate subtitle-fit presentation chatter

## Status

In Progress. The live VP Renderer trace from 2026-08-16 at
`00:26:23-00:26:49` reproduces a release-blocking regression. Work is isolated
on source branch `codex/vp-0132-subtitle-stability`, based on the current GitHub
default branch `v1.2.001-beta` at `74a6c7e`.

The trace review found that the configured screen aspect remained 2.35, but a
`BAR_CROP_TRUSTED` refinement that had not yet reaffirmed the retained crop
briefly withdrew the trusted
`0,276-3840,1884` presentation to full raster and restored it on the following
frame. Subtitle translation then published many per-frame layout rectangles.
The existing two-dense-sample confirmation can complete in only a small
fraction of a second and does not close every provisional-frame authority gap.

Implementation checkpoint (2026-08-16): source commit `68bd68f` requires three
compatible dense subtitle observations, retains the prior accepted target while
a replacement target confirms, and adds an explicit bounded retention state for
the recorded `BAR_CROP_TRUSTED` refinement that has not yet reaffirmed the old
crop. The crop log now publishes `bar_wait`. Focused Alpha source-crop policy
tests pass 99/99, including the exact non-containing bar-refinement state,
three-sample initial engagement, three-sample retargeting, full-raster override,
and stale-generation rejection. The fix is published in PR #65. A clean
VP-0131 + VP-0132 integration at `bb4ca90` rebuilt the x64 Release host, VP
Renderer, and native test targets successfully. Its focused policy suite passes
99/99 and its complete native suite reports 836/841 with the same five
pre-existing configuration/reference failures. The matched Release EXE and
renderer DLL were deployed at 00:53 after backing up the prior pair under
`C:\Videoprocessor\vp\backups\vp0132-before-bb4ca90-20260816-0053`; active
configuration was not changed. Live forward/rewind confirmation remains
pending.

## User story

As a scope-screen operator, I want subtitle fitting to wait for convincing
evidence and retain an accepted placement through brief contradictory or absent
samples, so subtitles remain visible without rapid crop/aspect flashes or
presentation chatter.

## Requirements

1. Retain the last safe, same-generation trusted presentation while a new
   one-edge subtitle/UI candidate is provisional or awaiting dense analysis.
   Pending confirmation must never publish full raster for an isolated frame.
2. Require at least three compatible dense analysis observations before a new
   subtitle translation is accepted. Confirmation is based on analyzed samples,
   not raw rendered-frame count.
3. Apply the same confirmation rule to a material same-direction target growth
   or direction change. Keep the accepted target while the replacement settles.
4. Do not cancel an accepted translation on one absent, provisional, generic
   Fit, or contradictory dense sample. Continue the accepted presentation under
   the existing configured subtitle hold; only sustained safe absence may begin
   the configured release interpolation.
5. Genuine trusted full-raster authority, source/raster generation changes,
   invalid geometry, or unbounded/broad live pixels retain immediate fail-open
   behavior.
6. Preserve configured engage/release interpolation as the only normal
   frame-to-frame motion. Detector jitter must not repeatedly restart or retarget
   that interpolation.
7. Add change-only diagnostics distinguishing candidate confirmation, accepted
   hold, release eligibility, and safety fail-open.

## Recorded reproduction

- At `00:26:23`, the source crop alternated twice between full raster
  `0,0-3840,2160` and trusted scope `0,276-3840,1884` while the detector reported
  provisional geometry around 2.382:1.
- At `00:26:28-00:26:29`, a lower-edge translation engaged and moved toward its
  target.
- At `00:26:45`, the full-raster/trusted-scope alternation recurred.
- At `00:26:48-00:26:49`, another lower-edge translation engaged.
- The interval emitted 84 final-layout updates. `screen_aspect=2.35000` remained
  constant, proving the visible event was crop/presentation arbitration rather
  than a viewport-profile change.

## Acceptance criteria

- A deterministic replay of the recorded provisional/accepted/gap sequence
  never selects full raster between same-generation trusted scope frames.
- One or two compatible dense subtitle samples do not move the picture; the
  third accepts one buffered target.
- A material target change requires three compatible dense samples and retains
  the prior accepted target until then.
- One or two NONE/FIT/contradictory samples do not cancel an accepted subtitle
  placement or start release.
- Sustained absence beyond the configured hold begins one release interpolation
  and settles on the trusted base without a full-raster frame.
- Trusted full-raster evidence and generation invalidation still fail open
  immediately.
- Focused Alpha crop/subtitle tests, the full native suite, and a clean x64
  Release build pass.
- Live forward playback and rewind across the reported scene show no aspect/crop
  flash and diagnostics show bounded confirmation and release transitions.

## Release boundary

This regression blocks release acceptance of the current VP Renderer candidate,
including PR #64/VP-0131, until the deterministic and live checks pass. VP-0132
does not change NLS-V crop semantics; it hardens shared subtitle-fit presentation
stability.

## Related stories

- VP-0098: arbitrary-CIH presentation envelope and screen fit.
- VP-0110: smooth subtitle placement and target confirmation.
- VP-0122: retain scope geometry through subtitle and volume overlays.
- VP-0129: vertical overlay arbitration.
- VP-0131: VP Renderer NLS-V and bounded presentation crop.
