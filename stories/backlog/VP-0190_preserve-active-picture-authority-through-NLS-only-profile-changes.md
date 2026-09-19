# VP-0190: Preserve active-picture authority through NLS-only profile changes

## Status

Backlog

Created 2026-09-19 at the user's request as a separate follow-up to the crop
review. Implementation has not started. The source review establishes a
conflicting reset path; a production-connected failing regression and playback
confirmation are the next steps, not completed acceptance evidence.

[VP-0189](../review/VP-0189_stabilize-automatic-crop-and-diagnose-dark-scene-transitions.md)
remains in Review. This story does not reopen or change its status.

## User story

As a VP user, I want to change only NLS presentation while a known picture is
paused or dark without losing its established active-picture geometry and
unnecessarily returning to Waiting.

## Problem and reviewed path

Source review on 2026-09-19 in the darkness-boundary-retention worktree identified
this inherited lifecycle conflict. Revalidate these named paths against the
current remote beta before implementation; this record does not prescribe that
worktree or its HEAD as the integration base.

1. Establish trusted full-raster picture geometry on a valid source.
2. Pause on, or transition into, dark content where the current measurement
   cannot independently reacquire geometry, while retained authority remains
   valid.
3. Apply a unified profile that changes only NLS selection or parameters and
   leaves source and effective viewport settings unchanged.
4. `CVideoProcessorDlg::ApplyUnifiedProfileSnapshot` selects shader profiles and
   applies the application profile. `SetConfiguredShaderSelection` deliberately
   preserves source geometry, but the application-profile path also calls
   `ApplyViewportTarget`, advancing its request serial even for the same target
   and aspect.
5. At the render boundary, profile-transition retention accepts a trusted bar
   crop but rejects trusted full-raster geometry. The transition/evidence reset
   also clears full-raster retention identity. The next dark observation cannot
   reacquire it, so NLS returns to Waiting despite no source change.

Relevant implementation is in `src/VideoProcessor-GUI/VideoProcessorDlg.cpp`,
`src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`, and
`src/VideoProcessor-Lib/vprenderer/AlphaSourceCropPolicy.cpp`.

This is a source-backed failure mechanism. Its exact user-visible duration and
trigger combinations still need a failing lifecycle regression and playback
validation; existing pure bar-crop retention tests do not establish those.

## Scope and safe design

Separate the lifetime of measured source geometry and supporting history from
the lifetime of NLS presentation intent. An NLS-only change on the same valid
source/raster should reuse established authority while applying the new NLS
rule, parameters, and On/Off state. Darkness must never create new authority.

- Classify the effective change at publication and consumption of immutable
  profile intent. Preserve source evidence only when the source identity and
  relevant geometry settings are compatible; retain appropriate presentation
  resets when they are needed.
- Preserve existing source-generation, format, dimensions, invalid-source,
  contradictory trusted-bar, and discontinuity vetoes. Old geometry must not
  survive a genuine source replacement or authorize a new picture extent.
- Apply real viewport, alignment, zoom, crop, subtitle, and padding changes.
  Audit settings comparison completeness: `EffectiveSettingsFingerprint`
  currently omits `screenEdgePadding`, although viewport application compares
  and updates it. Same target/aspect alone is not sufficient equivalence.
- Preserve queued A-to-B-to-A semantics: the final A request must replace a
  pending B even when A is still the currently published configuration.
  Compare against pending intent where required and consume a coherent snapshot.
- Do not merely admit full-raster geometry in `EvaluateProfileTransitionRetention`
  while the same path still clears supporting history and retention identity.
  Do not skip all apparently identical viewport requests using an incomplete
  fingerprint.
- Respect NLS Off, rule eligibility, stretch limits, and existing crop policy.
  Color-assisted evidence remains diagnostic-only and cannot authorize source
  geometry, retention, or stretching in this change.

This story covers the profile/source-lifetime conflict, not new crop thresholds,
new source-picture inference, window-placement behavior, or a broad profile rewrite.

## Acceptance and regression evidence

Create a regression that fails through production-connected profile publication
and render-boundary consumption before changing behavior. It must exercise shader
selection, application-profile application, queued intent, source geometry and
retention state, and the resulting NLS decision; a pure retention-helper test
alone is insufficient.

- With established full-raster authority followed by a dark unavailable
  measurement, an NLS-only change preserves source geometry and the applicable
  supporting history. It does not enter Waiting solely because of that change.
  An explicit Off request still disables NLS immediately.
- Cover NLS On-to-Off-to-On and profile A-to-B changes, including both retained
  full-raster and trusted bar-crop geometry. Presentation rules use the new
  settings while source authority remains tied to its original valid evidence.
- Unknown startup geometry still waits. Darkness, a stale measurement, or a
  profile toggle cannot establish full-raster authority.
- Source-generation, format or dimensions changes and invalid-source transitions
  invalidate old authority. Contradictory trusted bars and genuine aspect changes
  continue through normal detection and confirmation.
- A padding-only change and representative alignment, zoom and crop changes
  actually apply. An NLS-only optimization cannot swallow those settings.
- Queue A-to-B-to-A before the next render and verify the final A snapshot wins
  without mixed settings, lost intent, or false source resets.
- Exercise native analysis and the converted P010 path where they reach this
  lifecycle. Preserve existing crop, near-black, profile, and presentation tests.
- Add concise transition telemetry distinguishing a source-authority retain/reset
  from an NLS presentation change, with reason and source/presentation identity.
  Avoid repetitive per-frame logging.
- Complete an x64 Release build and targeted lifecycle suites, then replay a
  paused dark frame, bright/dark transitions, real multi-aspect transitions, and
  profile changes. Record commits, failing-before/passing-after evidence, and
  remaining user playback observations before moving to Review.

## Next action

At implementation readiness, trace current publication/consumption ownership and
record the source and presentation invalidation matrix. If that matrix requires
an architectural change beyond a bounded lifecycle fix, record a discovery spike
before expanding scope. Establish the failing end-to-end test, independently
review the retention boundaries, and then implement the smallest complete fix.
