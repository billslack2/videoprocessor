# VP-0213: Stable Off bar-content fit and release hold

## Status

Backlog

2026-10-25: User requested this story and investigation. Investigation has
started; production implementation has not. This remains Backlog under the
tracker workflow until the production ownership path and regression strategy
are ready. No runtime binary or active configuration changes.

Current GitHub beta and default branch: `v1.4.02-beta`, fetched at
`8c4537462bbabd6ce07ba3e9ffd9be9bc8745492`. Clean investigation worktree:
`E:\codex\videoprocessor\subtitles-off-zoom-hold-20261025`, branch
`codex/subtitles-off-zoom-hold-20261025`. The user's standing instruction
explicitly selects current remote beta for source work.

The reported runtime used matching host/renderer commit
`3520e71396a3c20112e4b18c67258052af34e0e0`. Off was selected at 10:42:33;
Classic was selected at 10:50:35 (America/New_York). At 10:50:21, consecutive
source decisions alternate final crop bottom 1974 / 2068 with intervals of
31 / 47 ms despite steady measured fit bottom 1974. Actual submitted geometry
also alternates, proving this reaches output rather than being log-only noise.

Evidence: [incident excerpt](../assets/VP-0213/investigation-20261025/incident.log)
and [initial investigation](../assets/VP-0213/investigation-20261025/findings.txt).
Root-cause reproduction and acceptance tests remain pending; no regression-
introducing commit has been identified.

## User requirement

When subtitle Processing style is Off, visible content in any black bar
(subtitles, menus, scrub controls, labels, or other graphics) must cause VP to
zoom out enough to include that content and the picture. Preserve geometry
and aspect ratio; allow black bars on all four sides when needed. Off performs
this fit behavior without Classic's picture translation or the reconstruction,
cleanup, or repainting used by other subtitle styles.

The view must remain stable while content is still visible. It must not
alternate between competing fitted extents, crop in and out between frames,
or wait for a subtitle-specific shape classification to permit visibility.
A real newly larger visible extent can require further zoom out; that must
not cause repeated expansion/contraction against unchanged evidence.

The optional X-second hold bridges brief disappearances between subtitles,
menus, or controls. Positive hold keeps the admitted fitted view until the
configured interval has elapsed after the last matching visible content.
Reappearing content renews its release lifetime. A held/replayed output alone
must not renew it indefinitely. Zero disables the release delay, not stable
behavior while content remains visible. The hold must not mask an ownership
oscillation that occurs before content has disappeared.

## Problem and observed evidence

The trusted picture stays at approximately `0,272-3840,1888` during the
incident. Measured fit evidence is consistently `0,272-3840,1974` across
source sequences 18851-18856, but final crop decisions alternate between
that rectangle and `0,272-3840,2068`. Both keep the same native picture AR;
the changing presentation envelope makes the picture visibly change size.

The expanded decision cites `uncertain picture preserves submitted AR and
measured outward pixels` and final fill cites `recovery preserves the complete
visible envelope; aspect-limit fill withheld`. Current measured evidence and
retained submitted presentation therefore disagree downstream of measurement.
The user reports that the bar content is still on screen during bouncing.
Do not assume the incident consists only of subtitle disappearance/reappearance.

## Scope

- Trace and correct Off's final fit selection, admitted/held presentation
  state, crop recovery, and source/output queue interactions as required.
- Cover one-edge and multiple-edge bar content, including sparse UI graphics.
- Keep native picture AR authority separate from the presentation envelope
  needed to show additional visible pixels.
- Apply the optional release hold consistently to the actual displayed fit.
- Preserve independent Classic, reconstruction, and other subtitle behavior.
- Preserve normal genuine picture-AR transitions, source generation resets,
  viewport changes, and current-pixel visibility guarantees.

Do not resolve this by increasing the default hold, adding arbitrary detection
latency, turning Off into Classic, suppressing all AR changes, or cropping
visible bar content. Avoid a broad detector rewrite unless the investigation
proves the fault is in native measurement rather than final presentation.

## Initial investigation and next actions

1. Trace steady current fit through `UpdateVerticalBarPresentation`,
   envelope composition, fit-ownership qualification, presentation admission,
   `ResolveUncertainPicturePresentation`, final fill, and successful output
   submission. Identify exactly where 2068 is retained or restored and which
   source/output snapshot supplies it.
2. Existing generic fit-hold eligibility requires both top and bottom
   expansion. The incident is lower-only, so that helper is not evidence
   that this case is covered. Trace the detailed measured Off FIT owner.
3. Existing sparse-control tests prove pixel measurement, envelope creation,
   and isolated hold expiry, but do not replay final recovery and actual
   successful temporal output feedback. Reproduce the production decision
   chain with source identity, current pixels, prior submission, and hold
   deadlines; include repeated display submissions and source/output offset.
4. Decide the smallest ownership correction that gives the displayed Off
   envelope one consistent lifetime and prevents older output from undoing
   current measured fit. Treat this as a hypothesis until replay proves it.
5. Move this story to In Progress with readiness evidence before production
   implementation. Validate final geometry and release timing, not merely
   intermediate detector rectangles or absence of logged errors.

## Acceptance criteria

1. With stable picture and visible lower-bar subtitle/menu/control content,
   Off fits the complete visible envelope and produces stable final source
   and target rectangles for a sustained interval. Repeat for upper, side,
   and simultaneous multiple bars. Four-sided black bars are valid output.
2. Reproduce the incident with native picture `0,272-3840,1888`, stable current
   fit `0,272-3840,1974`, and prior submitted envelope ending at 2068.
   The production pipeline must not alternate 1974/2068 across source frames
   or temporal submissions. Legitimate expiry of an older larger extent may
   cause one controlled change, followed by stable output.
3. With hold 0 and hold 2 seconds, content that remains visible never causes
   a return to picture-only framing or frame-to-frame fitted-size oscillation.
   Zero hold must not be needed to hide another policy's instability.
4. At a positive hold, a disappearance shorter than the configured interval
   retains the fitted view; reappearance renews it. After continuous absence
   reaches the interval, return once to the current picture-only fit.
   Verify default, fractional, zero, and non-default positive values.
5. Skipped/unavailable inspection and repeated output are distinguished from
   positive disappearance evidence. They must not alternate ownership, hide
   visible content, or refresh the hold without new supporting source evidence.
6. Further genuinely visible outer content expands coverage immediately and
   safely; nearer/new content and short gaps cannot repeatedly resurrect an
   older larger envelope. Support smooth/animated overlays without clipping.
7. Validate source-rate output and Smooth Motion with queue lookahead,
   repeated output and delayed successful submission; include source/viewport
   changes, real picture AR changes, and valid full-raster content. Stale
   generations cannot carry a held envelope into a new context.
8. Classic translation/hold/drift and reconstructed subtitle placement,
   compositing, and recovery pass their existing regression tests. Off never
   invokes those treatments to achieve stability.
9. Relevant focused tests and a successful x64 Release build are required
   before review. Live playback validation must record final submitted
   rectangles over the event and across release expiry; no deployment is
   implied by the investigation request.

## Readiness and validation boundary

Configuration is already modeled as subtitle profile `type: off` with
`off_zoom_hold_seconds` (0-30 seconds, default 2); no new setting is proposed.
Current unknown: which final recovery/submission ownership path reintroduces
the stale larger envelope and how its lifetime relates to the measured Off
FIT hold. Production implementation is deferred until a bounded replay can
prove that boundary. Investigation artifacts contain logs and text only;
no captured video pixels are committed.

## Related work and tracker audit

VP-0207 owns broader subtitle/temporal presentation work; this story isolates
Off's simple fit-and-hold semantics and the reported rapid fit oscillation.
Relevant existing commits: `6168c11e` (Off timer and sparse controls),
`b4004170` (immediate measured Off fit), `6608b736` (Off bounds independent of
coarse AR history). These are already in the deployed source; their presence
does not establish full-path stability.

Before assignment: 231 canonical files and index rows; maximum root 212;
registry last 212 / next 213 / total 231; no duplicate IDs or missing items.
Legacy exact-status formatting differences and the unrelated VP-0207 local
asset edit are preserved; see the audit artifact. The audit records the
existing VP-0168 index capitalization difference and VP-0070 missing exact
Status heading; these unrelated records were not repaired. New story/index/status
must agree exactly. Story changes are committed and pushed to origin/main.
