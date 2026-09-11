# VP-0179: madVR BT.2020-to-Rec.709 return regression

## Status

Review (2026-09-11). Diagnostic build ready in draft PR #91, source b70f8518,
based on current beta a33c5bf3. The reported Rec.709 return failure remains
unresolved; latest default-enabled recovery ZIP fails while the same August 1
1.15 beta succeeds. VP-0170 is a later repair attempt, not the original cause.

Added capture-to-madVR color tracing without changing recovery policy. Clean
x64 Release solution rebuild succeeded and 203 selected tests passed. The
verified diagnostic ZIP and guide are available for reporter reproduction.
PR #91 is open/draft, not merged. No deployment or active configuration edit.
https://github.com/billslack2/videoprocessor/pull/91

## User story

As a madVR user returning from HDR content to a Rec.709 menu/SDR content, I
want VP and madVR to update their input/output color contracts reliably,
without restarting the application or staying latched in BT.2020.

## Evidence and scope

User supplied cinillo2's report:
https://www.avsforum.com/goto/post?id=64830993
The user also reports faster rendering on an older version. GPU conversion
was built on the older working baseline, so it is a confounder and is not
evidence that the converter fixes color signaling. Compare native CPU builds
first, with the same configuration and source sequence.

Initial read-only source inspection used current deployed 04d3988c, based on
explicitly requested local VP-0174 16a8102f. DirectShow media type creation
sets transfer matrix, primaries and transfer function from effective video
state unless overridden. BuildPushVideoState and renderer rejection can cause
a restart for material state changes. VP-0170 separately stabilizes raw EOTF
with two matching valid observations across a 5000 ms window. This policy
tracks EOTF, not primaries, but a separate rejection/restart path exists;
its presence must be accounted for before claiming colorspace-only changes
are ignored. Raw versus effective LLDV EOTF/primaries need separate tracing.

## Investigation and acceptance

1. Obtain exact good and failing builds, source/receiver/display chain, madVR
   version/settings and VP configuration. Identify whether 'stuck BT.2020'
   refers to VP input OSD, madVR input matrix/primaries, projector information,
   or visibly incorrect colors. These identify different failure boundaries.
2. Capture logs beginning before HDR playback ends through at least 10 seconds
   of the SDR menu: raw DeckLink EOTF/colorspace/HDR block, effective overridden
   state, LLDV state, EOTF candidate/commit, renderer generation/restart and
   madVR runtime input/output status. Record HDMI display evidence separately.
3. Reproduce on matching CPU-converter builds/configuration and bisect between
   verified source revisions. Do not infer a source SHA from a calendar date.
4. Test PQ/BT.2020 -> SDR/Rec.709, SDR/BT.2020 -> SDR/Rec.709 (constant EOTF),
   transient metadata withdrawal, LLDV -> menu, and unchanged-resolution/rate
   transitions. Cover conversion NONE and P010 to isolate negotiation effects.
5. Confirm stable Rec.709 input reaches the intended media type/madVR state,
   stale HDR metadata clears, forced user overrides remain deliberate, and
   necessary graph rebuild occurs once without restart loops. Verify display
   signaling only with receiver-side evidence, not driver acceptance alone.
6. Implement only after the failing boundary is established; add regression
   tests for that boundary and perform repeated hardware HDR-to-SDR cycles.

## Related work, not duplicates

- VP-0170: EOTF transition stabilization (merged, hardware acceptance pending).
  This report is a distinct return-to-Rec.709 incident and may expose a gap;
  cross-link rather than assume VP-0170 caused or fixed it.
- VP-0151: VP Renderer NVAPI AVI InfoFrame exit, explicitly excluding
  DirectShow signaling. Do not merge this madVR report into that story without
  evidence that VP Renderer previously owned the display state in this session.
- VP-0178: CPU/GPU conversion investigation, deliberately separate.

## Source inspection locations

- src/VideoProcessor-GUI/VideoProcessorDlg.cpp: capture state, BuildPushVideoState,
  ObserveEotfTransition, LLDV exit and deferred renderer restart handling.
- src/VideoProcessor-Lib/EotfTransitionStabilizer.h/.cpp
- src/VideoProcessor-Lib/microsoft_directshow/video_renderers/DirectShowGenericHDRVideoRenderer.cpp
- src/VideoProcessor-Lib/microsoft_directshow/DirectShowTranslations.cpp

## Investigation update (2026-09-10)

Inspected the freshly fetched GitHub beta `v1.3.005-beta` at `89d55ca5`
(Merge PR #84), in clean detached worktree
`E:\codex\videoprocessor\vp-0179-investigation`. No source edits, build,
deployment or hardware reproduction performed. Story remains backlog pending
incident evidence. The linked AVSForum post could not be retrieved during this
pass; the report above is not independently verified.

### Established source behavior

- `DirectShowVideoRenderer.cpp:129-175` rejects changes to colorspace or EOTF
  on both the caller admission path and graph-thread path. The GUI rejection
  handler (`VideoProcessorDlg.cpp:5995-6055`) schedules a restart, with explicit
  LLDV and pending EOTF stabilization exceptions. Thus a constant-EOTF
  BT.2020 -> Rec.709 change is not generally ignored.
- `CaptureVideoStatePolicy.h:36-59` includes colorspace and EOTF in the material
  contract. The metadata-only ingress retention change `f9242793` does not
  classify a real colorspace change as metadata-only.
- `DirectShowGenericHDRVideoRenderer.cpp:473-486` derives media-type primaries,
  matrix and transfer function from effective state unless forced. Confirm
  those overrides before attributing output to raw capture metadata.

### Concrete gaps, not a reproduced root cause

1. `BlackMagicDeckLinkCaptureDevice.cpp:751-772`: unsuccessful EOTF or
   colorspace GetInt calls leave the cached value unchanged; loss of the
   metadata interface also skips those updates. If return-to-menu stops
   reporting these properties, VP can retain its previous raw contract.
   Blame dates this read-success-only behavior to `d71bde25d` (2021), so it
   is not evidence of a newly introduced regression. Capture each HRESULT,
   metadata-interface availability, frame flags and cached values to test it.
2. `DirectShowGenericHDRVideoRenderer.cpp:140-141` skips null HDR updates;
   `ALiveSourceVideoOutputPin.cpp:540-548` explicitly rejects null HDR data.
   Therefore metadata withdrawal with an otherwise unchanged contract does
   not clear the existing source's cached HDR block. The asynchronous guard
   dates to `ec59c0b85` (2026-07-29); the synchronous non-null-only behavior
   is older. This is relevant to metadata-only transitions, but does not by
   itself explain failure after a successful Rec.709 graph replacement.
   Removing the guard alone would hit the source's null rejection.

### Available runtime evidence and next discriminator

The configured guidance path `C:\logs\vp.log` is absent. Inspected available
`C:\Videoprocessor\vp\logs\vp.log` and rotations `.0` through `.8` instead.
They contain SDR EOTF baselines and madVR runtime examples with
`yuv_matrix='TV.709'`, `hdr_output=0`; the targeted searches found no pending
EOTF transition or BT.2020 raw-state evidence establishing this incident.
These are local-session observations, not the reporter's failing capture or
proof of HDR-to-SDR recovery.

Obtain exact good/failing source revisions and the same CPU-conversion setup.
For one reproduced exit, determine the first failing boundary:

- Raw VP remains BT.2020: inspect DeckLink read results and source HDMI state.
- Raw is Rec.709, effective remains BT.2020/PQ: inspect forced settings, active
  profiles and LLDV decision traces.
- Effective is Rec.709, madVR remains BT.2020: inspect rejection, deferred
  restart, replacement generation and negotiated media type.
- madVR reports TV.709/SDR but the display remains BT.2020: obtain receiver /
  projector signaling evidence; matrix alone does not prove HDMI primaries.

Do not select a bisect endpoint from the July 31 calendar date or implement a
speculative metadata reset before identifying the failing boundary.

## Deeper historical investigation (2026-09-10)

Found a source-level defect with a stronger temporal connection than the old
HDR-null handling: transient-invalid recovery can retain a stale valid color
contract indefinitely. This is a regression candidate for the reported madVR
incident, not confirmation of its hardware root cause. Scope remains the
reported madVR path; the faulty GUI/capture policy is shared code.

### Introduced behavior and historical boundary

`349fe22f65677d57066352fa106f0e7ce14b7a15` (2026-07-31 01:04 EDT),
"VP-0066 bound VP steady queue and defer transient signal loss", added both
invalid-state deferral and the counter-based dismissal below. It entered the
first-parent integration history in `d6dbd8bd` (2026-08-02), "Merge VP-0066
stable live delivery baseline". Verified that late-July integration reference
`785e5911` does not contain this commit and `d6dbd8bd` does. These are inspected
historical references, not claims about the reporter's exact working binary.
A July 31 development build could already contain the change.

### Failure mechanism

1. A running PQ/BT.2020 renderer receives an invalid capture state during
   HDMI resync. `VideoProcessorDlg.cpp:5835-5875` retains the previous valid
   state and starts a nominal 1500 ms grace period.
2. `BlackMagicDeckLinkCaptureDevice.cpp:678-695` increments the captured-frame
   counter from timestamps before examining the no-input flag at line 732.
   Consequently an advancing counter is not evidence of valid input or valid
   color metadata. Missing required metadata can also make the published
   VideoState invalid (`SendVideoStateCallback`, lines 1058-1064).
3. On expiry, `VideoProcessorDlg.cpp:14389-14413` discards the pending invalid
   state solely because that counter advanced. It kills the timer, releases
   the pending state and returns without BuildPushVideoState or UpdateState.
   It does not require any subsequent valid-state notification.
4. The GUI therefore retains its old valid color state and madVR can retain
   the existing HDR graph. The periodic EOTF sampler reads that same retained
   GUI state, not fresh DeckLink validity, so it supplies no independent repair.
   A later valid Rec.709 notification DOES cancel deferral and take the normal
   rejection/restart path; this defect cannot explain failure downstream of an
   already-applied valid Rec.709 state.

### Executed isolated verification

Compiled an x64 C++ harness with MSVC using the timer-handler block extracted
verbatim from `89d55ca5`; simulated capture counter and GUI side effects only.
Production source remained unchanged. Three assertions passed:

- Advancing counter 100 -> 190, no valid recovery notification: pending invalid
  state is discarded, previous valid state retained, neither push nor update
  called; still retained at simulated 60 seconds.
- Stationary counter 100 -> 100: invalid state applied and push/update called.
- Before the deadline: invalid state stays pending.

Harness saved at
`C:\Users\bslac\Documents\ChatGPT\Done\VP-0179-invalid-state-harness.cpp`.
It includes RendererIngressState.h from the investigated worktree. This proves
handler behavior, not a DeckLink/madVR hardware reproduction or an old/new
binary test. No claim that the old version necessarily reaches Rec.709 when
capture metadata remains invalid; its difference is that it applies invalid
state rather than silently retaining the previous valid HDR graph.

### Proposed narrow correction and validation

Keep a bounded grace period, cancel it only when fresh valid state arrives,
and apply invalid state on expiry even if callbacks continue. Repeated invalid
notifications should not extend the grace indefinitely. Reuse the existing
invalid-signal stop and subsequent valid-state restart path; do not guess SDR
or Rec.709, and do not alter forced color settings. Validate brief resync with
valid recovery, continuous no-input callbacks, persistent missing metadata,
repeated invalid notifications and eventual Rec.709 recovery. This change
could increase graph rebuilds for sources whose invalid interval exceeds the
grace, so hardware transition acceptance remains necessary.

Also inspected `93eb9370` (VP-0170): it replaced the older capture-restart
fallback with stabilized renderer-only restart and reset-operation deferral.
That is a separate behavioral difference worth testing if a capture restart
restores the reporter's state, but inspection did not establish a reset deadlock.
Do not restore the old unconditional capture restart on this evidence alone.

## Implementation ready for review (2026-09-10)

- Added InvalidCaptureStateGrace: one 1500 ms interval per invalid episode.
  Repeated invalid notifications retain the original deadline. Notifications
  arriving at/after expiry apply the invalid state even if timer delivery is
  delayed. Valid recovery cancels the pending timer and state.
- Removed callback/frame-count progress as proof of signal recovery. Timer
  expiry always applies the pending invalid state through BuildPushVideoState
  and UpdateState. CaptureStop clears deferred state before retiring the run.
- Added five native regression tests for deadline expiry, continuous invalid
  notifications, valid recovery, a new episode and capture-run reset.
- Source worktree: `E:\codex\videoprocessor\vp-0179-bounded-invalid-state`.
  Commit `5bad3a3f` pushed; source worktree clean. Draft PR #87 targets the
  current beta, not main. Hardware evidence is required before claiming the
  reported HDR-to-Rec.709 incident resolved.

Validation:

- Full `VideoProcessor.sln` x64 Release build succeeded (0 errors, 24 warnings)
  with the repository-configured toolsets. An initial forced-v143 attempt
  failed because that installed toolset lacks MFC; the configured v142 toolset
  uses the installed VS2019 MFC libraries and succeeded without source changes.
- VSTest: 23/23 passed across InvalidCaptureStateGraceTests,
  EotfTransitionStabilizerTests and RendererResetRequestLatchTests.
- An isolated MSVC harness using the extracted production notification and
  timer blocks passed five integration checks: timer expiry with repeated
  invalid notifications, notification-driven expiry under continuous traffic,
  valid recovery plus stale timer, later invalid episode, and early timer.
  This is simulated integration evidence, not a hardware test.
- Test results: `C:\Users\bslac\Documents\ChatGPT\Done\VP-0179-test-results`.
- No runtime configuration, deployed binaries or source beta branch changed.

Remaining acceptance: repeat hardware HDR-to-SDR/menu transitions and test
brief resync recovery within the grace, persistent invalid metadata/no-input
callbacks beyond it, repeated invalid events and eventual valid Rec.709 return.
The intended behavior can stop/rebuild the graph on sources whose invalid
interval exceeds the grace; do not interpret callback progress as validity.

## File-only A/B toggle (2026-09-10)

This supersedes the original unconditional correction described above.

- `[general] bounded_invalid_capture_recovery: false` (also the absent-key
  default) retains legacy counter-based dismissal and grace extension on
  repeated invalid notifications. `true` enables the bounded correction.
- Startup-only: restart VP after changing the file. There is no GUI field,
  command-line switch, or live application of the value. The startup log
  reports `Invalid capture recovery policy: mode=legacy` or `mode=bounded`.
- Strict Boolean schema validation and section ownership added. Editor
  round-trip tests confirm the internal setting/comment survives unrelated
  changes. The source example config documents the switch with false default;
  the user's deployed config is untouched.
- Fresh worktree `E:\codex\videoprocessor\vp-0179-recovery-toggle` started from
  fetched beta `30f36307`; the existing fix was merged, then the toggle was
  committed as `794cc80b`. Existing source worktrees were preserved. Pushed to
  the existing PR head `codex/vp-0179-bounded-invalid-state`; draft PR #87 updated.
- Full x64 Release solution build succeeded: 0 errors, 45 warnings.
  198/198 selected native tests passed (recovery policy, config-file schema,
  config-editor preservation, EOTF and capture-ingress suites).
- Five isolated production-handler checks passed: legacy advancing counter,
  legacy stationary counter, bounded deadline despite advancing frames,
  bounded notification expiry under continuous traffic, and valid recovery
  canceling stale timers in both modes. Hardware A/B acceptance is still open.
- Results: `C:\Users\bslac\Documents\ChatGPT\Done\VP-0179-toggle-test-results`.
- No merge, deployment, or active user configuration edit performed.

## General-section follow-up (2026-09-10)

At user request, the startup-only toggle now lives exclusively under [general]:
`bounded_invalid_capture_recovery: false`. Commit `3717c8b8`, pushed to PR #87.
All setting examples above use the current location. The former [internal]
section is no longer registered. False/absent remains legacy; true enables the
bounded correction; restart VP after changes. No GUI or command-line field.

Updated strict validation, startup loading/logging, source sample and parser /
editor-preservation tests. Full x64 Release clean rebuild succeeded after an
incremental linker debug-information failure. All 198 selected tests passed;
the editor fixture uses the canonical [vprenderer] format so [general] is
interpreted as startup settings. Results are in
`C:\Users\bslac\Documents\ChatGPT\Done\VP-0179-general-test-results`.
No active configuration or deployed binary changed. Hardware acceptance pending.

## Merge record (2026-09-10)

User authorized merge. PR #87 was mergeable with no GitHub checks reported;
verified head 3717c8b8 was merged using the exact-head guard into v1.3.005-beta.
GitHub confirms MERGED at 2026-09-10T15:55:28Z, merge commit cc04476a.
The opt-in default remains false. Deployment and hardware A/B acceptance are
not performed by this merge; the review state tracks that remaining validation.
## Reopened investigation and earlier restart workaround (2026-09-11)

The user confirms the failed test used the latest ZIP we built,
VideoProcessor-v1.3.005-beta-a33c5bf3-x64-Release.zip. Retesting the same
1.15 beta dated August 1, 2026 still returns correctly to Rec.709. Treat
VP-0179's invalid-state correction as insufficient for the reported incident.

The user recalls an Android/Fire TV source needing a special restart. Source
history verifies an older full-capture restart fallback, although the searched
code/story records do not name Fire TV or Android specifically:

- 47489950 (February 7, 2026) added a periodic comparison of current raw EOTF
  with the EOTF at renderer start. On mismatch it requested capture restart,
  which also rebuilt the renderer. Later fixes retained this fallback.
- The fallback exists in inspected historical 785e5911 and the v1.1.015-beta
  branch history. That branch's current tip is August 5, so it is not itself
  an exact identity for the reporter's August 1 binary.
- VP-0170 commit 93eb9370, integrated by 534c1e6a on September 7, deleted the
  full-capture restart block. Its replacement requires repeated valid EOTF
  observations over 5000 ms and then requests renderer restart only.
  https://github.com/billslack2/videoprocessor/commit/93eb937080e631732ac0fd9e0e67a7373dc3ee83
- These operations are materially different. Capture stop disables DeckLink
  input and detaches its callback; start re-enables input/format detection.
  Renderer-only recreation leaves capture running. If a source/driver needs
  input reinitialization to refresh colorspace metadata, rebuilding madVR
  alone cannot provide that recovery.

This removal is a concrete recovery-policy difference, not the original regression
cause: the user confirms the failure predates VP-0170. The removed fallback
was triggered by EOTF mismatch, so it does not by itself explain every
constant-EOTF SDR/BT.2020-to-SDR/Rec.709 case. VP-0170 also addressed observed
restart/HDMI-resync loops, so restoring its old independent timers wholesale
would discard that protection.

Further inspection confirms current valid colorspace changes are rejected by
the existing DirectShow graph and normally request replacement. The new
invalid-signal toggle does not change that valid-state path or restore capture
restart. Old metadata-read failures can retain cached EOTF/colorspace; that
code predates this regression but could interact with removal of the restart.

Next discriminating hardware check: while stuck, compare Restart Renderer
with Restart Capture, retaining logs from the failing exit and each action.
If only capture restart repairs the return, test one stabilized full-capture
restart per confirmed transition while keeping VP-0170's debounce/coalescing.
Do not claim that intervention is proven until exercised on the reporter's
source chain. No speculative source fix, merge, or deployment in this pass.

Local log inspection: the configured vp_debug.log is absent; available
C:\Videoprocessor\vp\logs\vp.log and indexed rotations were inspected instead.
A local September 11 00:42 exit reaches raw SDR/Rec.709 after a format resync,
but that session uses VP Renderer and is not the reporter's madVR failure.
Source worktree E:\codex\videoprocessor\vp-0179-madvr-return-followup remains
clean at fetched beta a33c5bf3.
### Chronology correction (2026-09-11)

The user confirms VP-0170 was an attempt to repair an already-broken version.
Therefore its removal of the full-capture fallback cannot explain the original
regression. The earlier elevation of that removal as a leading cause was
incorrect; retain it only as a possible difference in present-day recovery.

Compared 785e5911 with 93eb9370's parent: the old periodic full-capture EOTF
fallback, renderer-start EOTF baseline and event/timer checks are still present
before VP-0170, with no substantive change to those checks in that comparison.
The primary investigation window must end at an affected pre-VP-0170 build.
Investigate earlier capture publication, effective-state changes and renderer
lifecycle behavior that could prevent the existing recovery from taking effect.
No root cause or new fix is established by the full-capture-history finding.
## Diagnostic implementation and package (2026-09-11)

The additional pre-VP-0170 comparison did not establish a root cause. DeckLink
EOTF/colorspace success-only cache reads remain unchanged; the metadata-only
classification still marks EOTF/colorspace changes as material, and formatter
range changes do not replace DirectShow primaries/matrix translation. Earlier
capture/renderer lifecycle changes remain candidates requiring runtime evidence.

At the user's request, added normal-level diagnostics at these boundaries:

- DeckLink: existing metadata read HRESULTs, observed versus cached values,
  frame/input/HDR flags, missing interfaces and read-failure counts. First
  sample per capture run, up to one change report per second, and a ten-second
  steady heartbeat. Suppressed changes, including brief reversions, are counted.
  No extra metadata COM reads, per-frame allocation or recovery changes.
- Capture publication and UI: run/sequence, raw versus effective color state,
  admission result, ingress required/acknowledged/applied sequences, restart
  intents, reset deferral, invalid grace and EOTF candidate. Ten-second UI
  snapshots preserve evidence even when state notifications stop.
- DirectShow/madVR: offered and connected input media types, color-info flag,
  explicit overrides and HRESULTs, queried on the graph owner once per graph.
  Existing runtime samples now carry generation IDs. Existing null-HDR-update
  retention is logged without changing it. Recovery commands have markers.

Source: b70f851829e1a3cf7b3fabdfe329bbec9d7675d8 in
E:\codex\videoprocessor\vp-0179-madvr-return-followup. Branch pushed and clean.
Draft PR #91 targets v1.3.005-beta. Full clean x64 Release solution build:
0 errors, 45 warnings. An incremental final-identity build hit the known
LNK1103 debug-information error; clean rebuild succeeded with configured toolsets.
203/203 selected tests passed, including five new trace tests for heartbeat,
rate limiting, brief reversions, read failures and capture-run isolation.
Test results: C:\Users\bslac\Documents\ChatGPT\Done\VP-0179-diagnostics-test-results.

Package:
C:\Users\bslac\Documents\ChatGPT\Done\VideoProcessor-v1.3.005-beta-VP0179-diagnostics-b70f8518-x64-Release.zip
30,782,903 bytes, 58 files, verified byte-for-byte against canonical staging.
Both runtime binaries match the successful Release build. The package includes
only the example configuration, not an active VideoProcessor.cfg.
SHA-256: CB7925C0664A30FAA786E3613E7A6A6C5E315263621DD76F1643E243AC5E63D6.

Guide:
C:\Users\bslac\Documents\ChatGPT\Done\VP-0179-diagnostics-b70f8518-guide.md
Source guide: docs/VP-0179_COLOR_TRANSITION_DIAGNOSTICS.md.
Reproduce the same exit, wait at least 35 seconds, then compare Restart Renderer
with Restart Capture if still stuck. Preserve vp.log and numbered rotations
and identify which VP/madVR/display indication remains BT.2020. Hardware
acceptance remains outstanding; this is a diagnostic build, not a claimed fix.
