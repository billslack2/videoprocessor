# VP-0213: Stable Off bar-content fit and release hold

## Status

Will Not Do

2026-10-28: Superseded as separate work by [VP-0220](../review/VP-0220_validate-consolidated-off-fit-and-ar-evidence.md) and [PR #223](https://github.com/billslack2/videoprocessor/pull/223). PR #203 is being closed as superseded, not merged or accepted. The still-applicable Off implementation is retained in the consolidated candidate; playback acceptance and the final merge decision move to VP-0220. Historical evidence below is preserved.

2026-10-25: Off-only implementation is committed and pushed at clean source
`697c1bcd779891cadde993528c03a71f473e0653` in
[draft PR #203](https://github.com/billslack2/videoprocessor/pull/203), targeting
current beta/default `v1.4.02-beta`, base
`8c4537462bbabd6ce07ba3e9ffd9be9bc8745492`.
Source worktree: `E:\codex\videoprocessor\subtitles-off-zoom-hold-20261025`;
branch: `codex/subtitles-off-zoom-hold-20261025`.

The full x64 Release solution build succeeded. All 11 new Off regressions
passed, and all 479 selected related native tests passed (including those 11).
The control replay reproduces the competing submitted 1974/2068 envelopes;
the Off owner keeps final crop/target stable with hold 0 and 2000 ms. Host,
renderer and test-DLL hashes match normal Release runtime receipts; generated
host/renderer identities are clean at this commit.

[Validation evidence](../assets/VP-0213/review-697c1bcd-20261025/validation.json),
[team assessment](../assets/VP-0213/review-697c1bcd-20261025/team-assessment.txt),
and source design `docs/VP-0213_OFF_FIT.md` record the implementation boundary.
One user-requested sub-agent assessed both competing owners and reviewed the
Off-only integration. Transition/startup/NLS corrections are included. Shared
Classic/reconstruction predicates and behavior remain unchanged; VP-0214
tracks their separate recovery-ownership concern.

Live capture/GPU acceptance remains pending: inspect submitted source/target
rectangles while bar content remains visible, across hold expiry/reappearance,
and during real picture AR/profile transitions. Check timing telemetry because
Off now inspects every eligible source; differing black references can prevent
reuse of a previous scheduled dense scan. Deterministic policy tests are not
live performance acceptance. The Off-only review build was not deployed; combined deployment readiness is recorded below. Active configuration is unchanged.

Original incident: matching host/renderer commit
`3520e71396a3c20112e4b18c67258052af34e0e0`, Off selected at 10:42:33 and Classic
at 10:50:35 America/New_York. At 10:50:21 the measured fit stays at bottom 1974
while final/submitted crops alternate 1974/2068 every 31/47 ms. The introducing
regression commit has not been identified. [Incident excerpt](../assets/VP-0213/investigation-20261025/incident.log).

## Combined PR #177 / #203 deployment readiness

2026-10-25: The user explicitly requested deployment of #203 together with
both the deployed migration-safe #177 work and its latest responsive Config
follow-up. The exact combined source is clean and pushed at
`6b02334a83100645e8331d89f63a47797111e79f`, branch
`codex/pr177-pr203-deploy-20261025`, in
`E:\codex\videoprocessor\pr177-pr203-deploy-20261025`. It starts at freshly
verified beta/default `v1.4.02-beta` tip
`8c4537462bbabd6ce07ba3e9ffd9be9bc8745492`, includes #203's `697c1bcd`, the
migration-safe #177 integration `588f9a9e` (the same tree as deployed
`dedc85d9`), and final responsive follow-up
`83ff62acc4557dde176687db68609e24ebf7149d`. These feature integrations do not
merge either PR into beta.

The final full x64 Release solution build passed. All 684 selected related
native tests and all 24 focused Config cases passed. The latter cover all 22
PR #177 focused cases plus Off hold/subtitle-profile round trips. Responsive
widths 760-1920 were checked with the actual shipped chart runtime at 100%,
150%, and 200% scale. Host and renderer generated identities are clean at the
combined commit; all seven host/renderer/Config/test binary hashes match their
normal Release records. The source branch is recoverable on origin.

[Combined validation](../assets/VP-0213/combined-6b02334a-20261025/final-validation.json),
[Config results](../assets/VP-0213/combined-6b02334a-20261025/config-results.json),
and [native results](../assets/VP-0213/combined-6b02334a-20261025/off-related-tests.log)
retain the exact evidence. The first offscreen layout run omitted
`QT_QPA_FONTDIR=C:\Windows\Fonts` and failed a width assertion. Its evidence is
preserved separately. Matching the validated Windows font setup made the
same unchanged built executable pass; no product assertion was weakened and
no product change was required.

Deployment preflight passes for 14 binaries/records/symbol files and preserves
all seven detected root/Config configuration or state files. Runtime DLL
private dependencies already match this build. A guarded backup, staged hash
verification, host/both-renderer pair replacement and rollback procedure is
prepared. **Deployment is awaiting the user saving and closing the installed
Config editor (observed PID 55420).** It was not force-closed. Nothing has been
replaced or started, and no configuration has been changed. This is a pending
user action, not a test failure or a request for renewed deployment approval.
Live capture/performance acceptance of the Off fix remains pending, so this
story stays Review.
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

## Investigation and selected design

The source-owned measured/held FIT and output-owned uncertainty recovery had
legitimate but conflicting responsibilities. Fresh caption/bar proof is exact;
held Off bounds or a skipped dense scan can lose that proof. Recovery can then
restore a larger previously submitted envelope. Preparing source n before
committing n-1 lets n recover the n-2 view, sustaining two interleaved sizes.

Off now owns one source-driven envelope and an explicit released/native view,
qualified by native model base, generation, viewport, policy/continuity, hold
setting, source order and evidence deadlines. Raw all-bar visibility is unioned
with dense vertical content; no glyph/UI-shape gate is needed. Unsafe unbounded
content receives full-raster coverage. Each retained outer edge owns its own
release deadline. Unknown/duplicate/displayed output neither renews nor
releases source evidence. Old selected output cannot resurrect released state.

The final Off decision overrides generic presentation recovery before NLS/fill
without publishing a new native picture AR. Active accommodation suppresses
optional crop/fill and uses linear fit. Released native view resumes normal
optional fill/NLS. Accepted native changes, full-raster authority and picture
handoffs retire obsolete Off context. Immutable temporal snapshots carry the
resolved source view; successful submission is only a display record.

Readiness was recorded before implementation: configuration already models
Off/hold, cached raw measurements and native geometry are available, final
recovery/fill order and source/output lifetimes are known, and bounded replay
can prove the competing-history defect. The user explicitly authorized team
assessment and immediate implementation once that path was clear.

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

Configuration remains subtitle profile `type: off` with
`off_zoom_hold_seconds` (0-30 seconds, default 2); no new setting is introduced.
Implementation is gated on an explicitly selected Off profile and normal
preview, automatic crop, configured screen and native model geometry, yielding
to fixed crops and genuine picture transitions. Other modes do not enter it.

Automated coverage includes real production recovery/admission/fill/anchor
feedback, active zero/fractional/positive hold, release/reappearance, unknown,
duplicate/backward source evidence, independent outer-edge lifetime, context
changes, side/multiple bars, full raster and conservative raw/dense visibility.
479 selected existing/new crop/cache/temporal/startup/reset regressions pass.
Full renderer profile/transition/startup/NLS integration was also independently
source-reviewed and compiled. Live GPU/capture and measurement-cost acceptance
are still required before declaring the user incident resolved on this system.
No captured video pixels are committed.

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
