# VP-0033: Alpha renderer libplacebo frame-mixing smooth motion

## Status

Review — implemented and merged into beta and DV2; requested build/configuration checks passed. Awaiting user review of controls.

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

## Implementation progress — 2026-10-14

User authorized full Motion/chroma configuration and merges into current beta and DV2. Validation is narrowed to VP configuration loading, persistence and libplacebo integration; visual-quality comparisons are not required for this request. Beta PR [#158](https://github.com/billslack2/videoprocessor/pull/158) merged; DV2 is preserving its independent additions and newly merged subtitle/aspect-ratio fix before retesting. No deployment or active configuration changes.

## Merged implementation — 2026-10-14

- Beta: [PR #158](https://github.com/billslack2/videoprocessor/pull/158), merge `f06430fd1d081cc0d5fdbddec1b45d9671eac34f`; tested source `304422d1f8616e59a7780a2ab5956c19fc57ef82`.
- DV2: [PR #159](https://github.com/billslack2/videoprocessor/pull/159), merge `4c080e8d5f2d2d472c85ed1393aeeae5ff98db56`; tested source `ead3f04552e1d0b56eb6c2170752f533e9c67224` preserves DV2 base `4dac8b8658033f8c123614a232ab07fce4323b0f`, including its new subtitle/aspect-ratio fix.
- Both merged trees are byte-for-byte identical at the source-tree level to their tested feature commits. Separate clean x64 Release builds passed, including stable and beta libplacebo plugins.
- Beta: 259 native integration checks and three focused Qt checks pass. DV2: 324 native checks, including tunnel/luminance/analysis diagnostics and renderer reset policies, plus four focused Qt checks including existing DV/LLDV controls, pass.
- Motion page exposes Smooth motion Off/Auto/Always (default Off), all four bundled mixers, Deinterlacing Auto/Off/Force, Weave/Bob/YADIF/BWDIF (default BWDIF), reported/overridden field order, field/frame cadence and YADIF spatial-check control. Scaling exposes explicit independent chroma reconstruction, default Bicubic without Auto. Compact tabs retain one row with scroll overflow.
- Capture field order, owned bounded GPU history, timestamps, field submission, discontinuity/reset behavior and configuration persistence are validated. Historical visual-quality comparisons are not required for this request: the user explicitly narrowed testing to VP configuration and libplacebo integration.
- Source subtitle cut/paste composition currently bypasses temporal processing with a diagnostic. Native HDR passthrough remains deferred. No playback/deployment or active configuration edits were performed.
- [Build/test receipts and Motion screenshot](../assets/VP-0032-0033/merge-20261014/README.md). Host/plugin API is 26 on beta and 28 on DV2; matched binaries are required within each branch.

## DV2 deployed — 2026-10-14

User authorized deploying DV2. Deployed exact merged `dv2` tip `4c080e8d5f2d2d472c85ed1393aeeae5ff98db56` from a successful clean x64 Release solution build. Host, stable/beta renderer plugins, Config UI, Config discovery and output probe plus matching runtime records and HTML help were backed up and updated; all copied hashes match build artifacts. One differing Qt offscreen plugin was also backed up and updated; remaining dependency hashes already matched.

Backup: `C:\Videoprocessor\vp\deployment-backups\dv2-4c080e8d5f2d-20261014-133826`. Active `VideoProcessor.cfg` and `VideoProcessor.state` remained byte-identical; no configuration edits. 199 focused config/parameter/motion/plugin checks passed. The deployed Config app opened Motion against a disposable copy of the operator config, exited successfully, and showed default Off smooth motion and Auto/BWDIF deinterlacing. VP playback was not launched. [Deployment receipt, build/test logs and screenshot](../assets/VP-0032-0033/deploy-dv2-20261014/README.md). Review status is unchanged.

## Motion UI explanation follow-up — 2026-10-14

User reported blank tooltips and unclear relationships between Motion controls. [Beta PR #160](https://github.com/billslack2/videoprocessor/pull/160) merged at `c71b44593cad5ba5dd8bd7ec369f0d3a5965ff72`; [DV2 PR #161](https://github.com/billslack2/videoprocessor/pull/161) merged at `2a2b4242dfc950381fbdc15c4125ca4ead910f82`, preserving DV2 additions.

Smooth motion and Deinterlacing now have compact named sections and inline explanations. Mixing filter is grouped with smooth motion; Algorithm and Field order with deinterlacing. Advanced deinterlacing holds cadence and YADIF-specific tuning. Each Motion control and label has meaningful hover help covering scope, defaults and choices. The profile loader no longer emits newline-only blank tooltips. Playback and persisted option values are unchanged.

Focused x64 Release Qt checks passed on both branches for grouping, all seven Motion tooltips, explicit mixer relationship, persistence and every-page round trips; the DV/LLDV mode preservation check also passed on DV2. [Actual 1040x700 layout](../assets/VP-0032-0033/motion-help-20261014/motion.png). Replacement deployment is pending Release build completion and closure of the user's currently running VP/Config applications; no process was interrupted.
