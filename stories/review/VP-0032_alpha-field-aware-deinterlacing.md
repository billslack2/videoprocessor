# VP-0032: Alpha renderer field-aware GPU deinterlacing

## Status

Review — implemented and merged into beta and DV2; requested build/configuration checks passed. Awaiting user review of controls.

## User story

As an Alpha renderer user with interlaced input, I want VP to recognize field
structure and use libplacebo's GPU deinterlacing so 1080i/576i/480i material is
presented cleanly at the intended field-derived cadence rather than being
treated as progressive or woven incorrectly.

## Libplacebo capability and boundary

The bundled libplacebo build provides weave, bob, YADIF, and BWDIF. YADIF and
BWDIF require correctly ordered previous/current/next field references.
Libplacebo performs the pixel processing; VP must supply accurate field order,
timestamps, frame lifetime, and presentation pacing.

## Scope

- Alpha/libplacebo renderer only.
- Accept interlace metadata from VP capture/media negotiation and retain it
  through queueing and renderer resets.
- Provide a safe policy such as `AUTO`, `OFF`, `BOB`, `YADIF`, and `BWDIF`,
  defaulting to unchanged progressive behavior unless interlaced input is
  positively identified.
- Present field-derived output at the correct cadence, for example 29.97i to
  59.94 Hz where the display path supports it.

Do not change the established renderer, invent interlace metadata when capture
does not provide it, or add motion-compensated frame interpolation.

## Required implementation work

1. Trace capture format/field-order metadata into the Alpha frame submission
   path. Define explicit behavior for unknown, progressive, top-field-first,
   bottom-field-first, and mixed/discontinuous streams.
2. Retain coherent previous/current/next frames while respecting VP's source
   buffer ownership, low-latency queue policy, reset generation, and GPU
   resource lifetime.
3. Populate libplacebo's field/deinterlacing inputs and choose a safe default
   algorithm. Never request a temporal algorithm without valid references.
4. Couple field timestamps to actual display cadence and refresh switching.
   Avoid duplicate/drop loops when changing from interlaced to progressive or
   when display-rate switching fails.
5. Add OSD/log diagnostics for input field state/order, selected algorithm,
   reference availability, derived output rate, fallback reason, and resets.
6. Add configuration/help only after the automatic safe path is demonstrated.

## Verification

- Test known TFF and BFF 1080i/29.97, 1080i/25, 576i, and progressive control
  samples with motion, diagonal detail, scrolling text, and hard cuts.
- Verify BWDIF/YADIF use only valid temporal references, Bob works without
  references, and unknown/progressive input remains unmodified.
- Confirm field cadence, refresh-rate selection, renderer resets, color/HDR
  metadata, queues, latency, and source-buffer release remain stable.
- Compare against the established renderer and a trusted reference for visible
  combing, motion artifacts, and cadence correctness.

## Acceptance criteria

- Alpha never deinterlaces progressive input by mistake.
- Correctly signaled interlaced input is rendered with a documented selected
  method and correct field-derived cadence.
- Missing references or uncertain metadata safely fall back without crashes,
  queue starvation, or presentation loops.
- Existing progressive Alpha playback is unchanged when the feature is off or
  unavailable.

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

The exact merged DV2 commit `2a2b4242dfc950381fbdc15c4125ca4ead910f82` subsequently completed its clean x64 Release solution build. Production Config opened successfully against a disposable copy of the active config and displayed the corrected grouping. [Build log](../assets/VP-0032-0033/motion-help-20261014/dv2-release-build.txt) and [native preview](../assets/VP-0032-0033/motion-help-20261014/native-motion.png) are saved. Replacement deployment remains pending closure of VP and Config, requested from the user; the prior `4c080e8d5f2d` deployment remains installed.

User authorized and completed replacement deployment of DV2 `2a2b4242dfc950381fbdc15c4125ca4ead910f82` at 2026-10-14 14:01 EDT. Host, both renderer plugins, Config and matching runtime records were backed up at `C:\Videoprocessor\vp\deployment-backups\dv2-2a2b4242dfc9-20261014-140116` and replaced with hash-verified x64 Release outputs. Configuration and saved state remained byte-identical. Deployed Config opened the corrected Motion page against a disposable config copy and exited successfully. Playback was not launched. [Deployment receipt](../assets/VP-0032-0033/motion-help-20261014/deployment.json) and [deployed screenshot](../assets/VP-0032-0033/motion-help-20261014/deployed-motion.png). This supersedes the pending deployment note above; Review status remains unchanged.
