# VP-0214: Audit shared subtitle fit ownership and temporal recovery

## Status

In Progress

2026-10-25: Follow-up concern identified during the independent team assessment
for VP-0213. Investigation/implementation of shared subtitle behavior is not
part of VP-0213; no Classic/reconstruction policy change is made by that repair.

## Focused backing-sliver dropout — 2026-10-27

User clarifies that AR and reconstructed subtitles remain stable; only the small
black lip under the blurred replacement flickers. Earlier 12:52–12:53 crop
movement examples are unrelated to this report. The correct reconstruction
interval is 12:50, on runtime `f4219a7a`.

At 12:50:46, cue 11 frames 1046/1047/1048 record cleanup on/off/on. A rounded
right endpoint contracts three native pixels: YUV 6/511/508 at (2562,1746)
exceeds dark limit 4 inside the old two-pixel collar. Revalidation rejects the
whole lip; the next frame reacquires a narrower outline. At 12:50:27, cue 3
frame 600 cold-acquires after a four-row native-boundary shift despite the same
composed edge, then rejects closure with one dark sample out of 1079.
The lip header is identical between observed runtime and current beta; recent
AR changes causing this symptom are not established by these logs.
[Focused telemetry](../assets/VP-0214/lip-dropout-20261027/focused-incident.txt).

Draft [PR #216](https://github.com/billslack2/videoprocessor/pull/216), clean
`18c66791443b7787851a8dd5798781f0b345e6f5`, starts from freshly fetched beta
`60df2bf6`. It excludes a contiguous faint luma fringe from cleanup within a
six-pixel edge bound while keeping the original dark/neutral threshold for
every erased pixel. Interior holes, brighter/wider intrusion, ambiguous
boundaries and incomplete work still fail. It revalidates a finite outline
through one native sampling step only with current boundary proof and the
exact same composed edge; metadata alone supplies no authority. Existing
telemetry adds `luma_fringe_pixels`. Detector AR and subtitle placement logic
are unchanged.

The new same-cue endpoint regression fails on original code with the logged
reason. Final x64 Release build succeeds; 150 backing-lip and CPU/GPU composition
tests pass. Coverage includes P010/P210/decoded DV/RGB, both bars and endpoints,
full/limited black, repeated trim/restore, current nearby-boundary proof and
missing-proof controls, live interior rejection, budget limits, and float/extend
GPU coverage. These are synthetic native-frame reproductions of measured
conditions, not replay of the exact incident pixels. Not deployed or merged;
live Ben-Hur acceptance remains pending. Story remains In Progress.
[Validation](../assets/VP-0214/lip-dropout-20261027/validation.json).

### Authorized test deployment — 2026-10-27 13:30 EDT

Deployed clean x64 Release integration `837afc5c838f857478421b9d9825cdf27a079114`
from pushed branch `codex/lip-ar-test-integration-20261027`. It combines the
PR #216 fix with the previously installed AR commits `daad7eb0` / `f4219a7a`
on current beta `60df2bf6`. AR implementation files match the prior runtime;
the current beta's VSync scheduler fix is also retained. The first incremental
build failed with LNK1103 debug-information corruption; a full solution Rebuild
succeeded. Completed isolated validation passed 573/573 cleanup, composition,
AR, lookahead and VSync tests. An extra broad GPU run was intentionally stopped,
and one focused runner exited without a final result or Windows crash record;
neither incomplete run is counted as qualification.

The user's worse-playback report occurred before deployment. At inspection,
PID 40968 was running older clean `376e46a3` from
`C:\Videoprocessor\vp-test-20261026-1954`; its logs were preserved separately.
It was stopped for this deployment. All nine host/renderer/runtime-record/PDB
files were backed up and replaced; 18 matching dependency DLLs were retained.
Backup: `C:\Videoprocessor\vp\backup-before-lip-fix-20261027-133003`.
Configuration and state were unchanged by deployment. An initial pre-replacement
state guard caught a changed user selection and stopped before copying runtime;
the proposed selection edit was removed. Latest saved Clear/Yellow selection
was preserved, then subsequent live selection changed to Classic after startup.

Main installation restarted as PID 42528 at 13:30:04. Startup logs confirm both
host and renderer at `837afc5c`, clean, with effective VSync enabled. The user
is now testing the new fix. Physical acceptance remains pending; PR #216 is
still draft/unmerged. No settings or binaries in the alternate test directory
were overwritten.
[Combined validation](../assets/VP-0214/lip-dropout-20261027/deployment-validation.json),
[deployment receipt](../assets/VP-0214/lip-dropout-20261027/deployment-receipt.json).

### Advanced subtitles plus Smooth Motion follow-up — 2026-10-27

After testing `837afc5c`, the user isolates the remaining small backing sliver
flicker to advanced subtitles with Smooth Motion. AR is stable. Preserved log
confirms mode 3, temporal=1, mixing=1 and a fixed source rectangle
0,384–3840,1776 during advanced playback. Native lip samples include valid
cleanup and separate no-attachment/context rejections; sampled logs cannot
prove every blended output pixel. No blanket claim that native cleanup is
always valid is warranted.

History search found a concrete integration gap: complete-picture preparation
`082a4aab` and source-event/transactional recovery `592d890b` are not ancestors
of the deployed source. The relevant complete-preparation functions and
ReconstructionMotionTests.cpp are also absent. Their earlier tested lineage
remains in OPEN DRAFT PR #197. Current code uses separate cleanup/layer
preparation, combines each packet before mixing, and retains that older source
packet/cache contract. The prior rounded row-mask repair remains present.
The 573-test deployment qualification did not include the absent dedicated
ReconstructionMotion tests, so it does not qualify the missing historical fix.

This is a verified source integration gap, not yet proof that importing PR197
will fix the exact visible sliver. Next work needs a joined production-path
regression with current row masks, 23.976-to-59.95 mixing, inserted outputs,
same-source retries/late cues, fixed picture geometry and captured pixel checks;
then review the earlier fixes against current AR and backing protections.
Do not import the whole old draft blindly or change AR thresholds based on this
symptom. No source change or additional deployment was performed in this audit.
[Investigation evidence](../assets/VP-0214/lip-smooth-motion-20261027/investigation.json).

### Combined Smooth Motion test deployment — 2026-10-27 14:45 EDT

User authorized incorporating the historical repair into the test build. Clean
x64 Release `53bb0f58bf200db5ab70c9233412f8e079a84895` is pushed on
`codex/lip-smooth-motion-test-20261027`. The clean worktree started at remote
beta `60df2bf6`; current official beta `df33935f` (Classic PR217) was subsequently
merged, with no additional file changes because the same Classic work was
already preserved. The concurrently installed Classic test `cc20c64e` was
included alongside the prior AR and focused backing-lip changes.

The narrow port includes historical `082a4aab` / `592d890b` and required
prerequisites: complete progressive reconstruction before library mixing,
exact-source packet reuse, transactional late current-worker recovery, bounded
complete-packet fallback, and GPU-program preservation across source gaps.
Current native detection, AR proofs/scheduling, rounded row masks, focused lip
repair, Classic behavior, VSync scheduler and shader-preparation grace remain.
The old broad AR/upload normalization and older native current-cue inspector
were deliberately excluded. Host and both renderer DLLs use ABI 30 together.

Validation passed 1,152 checks: standard GPU/cue 203, beta GPU/cue 203, native
CPU 725, and HDR 21. Native UHD reconstruction regressions cover rounded backing
intrusions with glyphs wholly in the bar, float/extend placement, mixed and
repeated 23.976-to-59.951 outputs, cue changes/end, late current-cue recovery,
context invalidation, bounded holds and failed preparation/rollback. A raw-lip
negative control proves the pixel oracle detects unrepaired output.

The first standard runner reported a manifest error after all 203 executed
tests passed: it named an upstream-only test class absent from this focused
port. The final commit removes only that nonexistent class from the runner;
all seven compiled classes passed. Standard/native/HDR results at `1ef9e0e6`
are retained with identical production, test and dependency trees at final
`53bb0f58`; the final full Release build and beta suite ran at that exact HEAD.
Initial evidence is retained; no failing assertion was waived.

Deployed nine verified runtime/sidecar/PDB files; 18 matching dependency DLLs
were retained. Backup:
`C:\Videoprocessor\vp\backup-before-smooth-motion-port-20261027-144554`.
No configuration or state edits were made. Restarted PID 8500 at 14:45:55;
startup logs confirm both host and renderer at clean `53bb0f58`, and installed
hashes match the qualified build. Current saved Classic selection is preserved;
advanced-subtitles plus Smooth Motion live replay remains pending. Controlled
regressions do not establish the exact live incident cause or acceptance.
Story remains In Progress. Neither PR197, PR216 nor this test branch was merged
into beta by this deployment.

[Integration provenance](../assets/VP-0214/lip-smooth-motion-port-20261027/integration-provenance.json),
[validation](../assets/VP-0214/lip-smooth-motion-port-20261027/deployment-validation.json),
[deployment receipt](../assets/VP-0214/lip-smooth-motion-port-20261027/deployment-receipt.json).

### Subtitle-only merge into beta — 2026-10-27 18:11 EDT

User authorized merging the remaining subtitle fixes. Created a clean worktree
from freshly discovered/fetched official beta `e263fda5`, then selectively
integrated the lip repair and qualified Smooth Motion changes from the test
build. All 32 changed subtitle-related files are blob-identical to deployed
`53bb0f58`; independent adaptive-AR commits and their fixtures are excluded.
Production AR files remain at current beta. The newer Config mixer-order change
already on beta is retained. No old broad AR/upload normalization or additional
diagnostic-counter work from draft PR197 is imported.

Fresh clean feature `ec54b685c157cd6f10cf860b6c7ea8e0368628f4` passed the full x64
Release solution build. Both required full GPU/cue suites passed 203/203 each;
native/HDR tests passed 735/735, and the mixer-selection Config check passed.
This is 1,141 native/GPU/HDR cases plus one Config check on the focused
integration, with no assertion failures or missing selected classes. The
native count differs from the earlier combined deployment because independent
AR changes and their extra tests were intentionally excluded. Source/assembly,
TRX hashes, selected dependencies and Release artifact hashes are recorded.
Physical Ben-Hur acceptance remains separate from these controlled tests.

Merged [PR #219](https://github.com/billslack2/videoprocessor/pull/219) into
`v1.4.02-beta` as `52f625c113191511f2890775fe9bdd8e511a7049`. The fetched official
beta contains that commit; its tree exactly matches the qualified feature tree
`19c3a6fbbb873735cf22f9cbbe6de94dfccb6d4f`. Closed superseded PR216 after verifying
its full focused lip repair is included. Broader draft PR197 remains separate.
No new deployment, player restart or active configuration/state edit was made
for this merge. The broader VP0214 investigation remains In Progress.

[Merge receipt](../assets/VP-0214/subtitle-motion-merge-20261027/merge-receipt.json),
[qualification](../assets/VP-0214/subtitle-motion-merge-20261027/merge-validation.json),
[scope comparison](../assets/VP-0214/subtitle-motion-merge-20261027/scope-comparison.json).

### Merged-beta deployment — 2026-10-27 19:11 EDT

User requested deployment after merge. Freshly discovered/fetched official beta
remains `52f625c113191511f2890775fe9bdd8e511a7049`. A new clean worktree at that
commit passed the full x64 Release solution build. The entire source tree is
identical to qualified PR head `ec54b685`; the 1,141 native/GPU/HDR results are
reused with their recorded TRX hashes, rather than rerunning unchanged suites.
Fresh binaries identify the actual merged commit; prior binaries were not
relabeled. Config/discovery code is unchanged from installed beta and its open
sessions were preserved.

Replaced nine host/standard-renderer/beta-renderer/sidecar/PDB files together;
18 identical dependency DLLs were retained. Prior running host/renderer were
`e263fda5`. Backup:
`C:\Videoprocessor\vp\backup-before-merged-subtitles-20261027-191144`.
No configuration or state edits were made; both file hashes also matched after
restart. Saved Classic selection and VSync setting were preserved.

Restarted the main installation as PID 39792 at 19:11:45. Startup logs verify
clean matching host and renderer `52f625c1`, both renderer plugins load API 30,
and all installed runtime hashes match the successful Release build. Physical
playback acceptance remains separate. Deployment evidence is synchronized here.

[Validation](../assets/VP-0214/deploy-subtitle-merge-20261027/deployment-validation.json),
[deployment receipt](../assets/VP-0214/deploy-subtitle-merge-20261027/deployment-receipt.json).

### Separate fullscreen-exit presentation failure — 2026-10-27

After merged-beta deployment, the user reported Failed renderer on fullscreen
exit while capture continued. Preserved telemetry establishes fullscreen exit
at 19:23:34, a zero-area windowed preview (swapchain clamped to 1x1), repeated
DXGI_STATUS_OCCLUDED with removed_hr=0, and fatal 300-visible-failure escalation
at 19:23:57. ModernOperatorLayout can produce a zero-area preview when the
window is narrow. IsWindowVisible alone does not prove a drawable target.
The checked-Present path and counter conflict already exist in the earlier
VSync commit 727467e7; subtitle reconstruction is not implicated by this trace.

Clean repair `7059c944c7e75e63c3e7d098e83772d94ecd4711`, based on current beta
52f625c1, distinguishes hidden/minimized/zero-area or rendered-but-occluded
presentation pauses from genuine failures. GPU failures retain priority;
visible draw failures retain their existing threshold. Per-attempt occlusion
resets prevent stale state from concealing shader/acquisition errors. Expected
pauses reset the streak and do not inflate failure drops. Repeated unavailable
Present logs are throttled. No subtitle preparation, packet lifetime or AR
logic changed. Draft [PR #220](https://github.com/billslack2/videoprocessor/pull/220)
is pushed and unmerged.

Full x64 Release build and 111 focused presentation-health/layout/readiness/
VSync regressions pass, including four new cases for the Modern narrow preview,
visible occlusion, GPU-failure precedence and current-attempt ownership.
Test-deployed host and both renderer libraries match the qualified artifacts.
Backup: `C:\Videoprocessor\vp\backup-before-windowed-health-20261027-193401`.
PID 39560 restarted at 19:34:02; configuration and state hashes are unchanged.

Live keyboard verification completed two fullscreen-to-windowed cycles. A
minimized 0x0 target reported presentation deferred/failure_streak=0 through
about 50 seconds of DXGI occlusion, then resumed successful submissions after
restoration without rebuilding the renderer. A second fullscreen/windowed
cycle also submitted successfully. No fatal renderer event occurred. VP was
left windowed. Screenshot capture timed out; evidence is the observed keyboard
actions and matching runtime logs. Exact narrow-dashboard geometry is covered
by the native layout regression, not a claimed live resized-window test.

[Incident](../assets/VP-0214/fullscreen-windowed-failure-20261027/incident-summary.txt),
[validation](../assets/VP-0214/fullscreen-windowed-failure-20261027/deployment-validation.json),
[live verification](../assets/VP-0214/fullscreen-windowed-failure-20261027/live-verification.json),
[deployment](../assets/VP-0214/fullscreen-windowed-failure-20261027/deployment-receipt.json).

## Problem

Classic and reconstructed subtitle modes share a boundary between exact fresh
caption/UI fit proof, held presentation extents, uncertainty recovery, and
successfully submitted source envelopes. These have different lifetimes.
An old selected output is a record of displayed geometry, but must not become
fresh source evidence or bypass an owner's bounded release semantics.

VP-0213 confirms an Off incident: source n can recover the envelope last
submitted from n-2 because source n-1 is selected after preparing n. Held
geometry may fail exact fresh-proof equality, allowing alternating envelopes.
This establishes an architectural risk in shared code, not a confirmed
reproduction or approved blanket fix for Classic/reconstruction.

## Scope and next action

Audit each mode's legitimate fit, translation, drift, composition and recovery
ownership against source evidence and actual selected output. Reproduce any
shared-mode failure independently before changing behavior. Preserve current
glyph/masking proof, no-clipping safety, release timing, and real AR transitions.
Start from current GitHub beta under the normal source-worktree workflow.

## Acceptance criteria

1. Document which evidence can originate, retain, replace and release each
   Classic/reconstruction presentation owner, including skipped/unknown scans.
2. Replay mismatched fresh/held bounds and delayed/repeated/failed submissions
   through production recovery and final output. Prove or dismiss the risk
   separately for each mode; do not claim Off evidence proves a shared bug.
3. If a defect is reproduced, implement a bounded mode-appropriate correction
   with source identity and lifetime tests, preserving glyph proof, translation,
   drift and composition. Passing tests must verify final source/target geometry.
4. Validate Off remains governed by its independent VP-0213 fit-and-hold owner.
   Record Release build/regressions and live-playback limits before review.

## Dependencies and evidence

VP-0213 owns the Off-only fix. Related code at beta 8c453746:
UpdateVerticalBarPresentation, QualifyConfirmedFitOwnership,
HasConfirmedPresentationCandidate, ResolveUncertainPicturePresentation,
TemporalPresentationSnapshot and CommitSubmittedNativeScopeAnchor.
See VP-0213 incident/investigation assets for the independently observed Off
case and source/output ordering. This follow-up must not delay the isolated fix.

## Independent Reconstruction incident - 2026-10-25

VP-0207 now records a bright fixed-ratio live failure at 22:52:59-22:53:04, independently of the earlier Off reproduction. Trusted native picture remains 0,278-3840,1882 while actual submitted geometry expands to 0,270-3840,1960/1978 and returns to 36,278-3804,1882. Reconstruction composition loss precedes each expansion; prepared subtitles restore native framing. Source generation 2 and viewport 4 remain current, near_black=0, scene=0, temporal=0 and mixing=0. Later deliberate subtitle cycling does not explain this interval.

See [incident receipt](../assets/VP-0207/fixed-ratio-subtitle-handoff-20261025/incident.json) and [actual output excerpts](../assets/VP-0207/fixed-ratio-subtitle-handoff-20261025/incident-excerpt.txt). This confirms shared-mode presentation instability, but not that the Off feedback defect is its cause. No matching native pixel capture proves whether no-bright-ink was an incorrect scan. Investigation found no stale-worker/reset evidence in this interval. Reproduce cue handoff and recovery with final geometry assertions before implementation; preserve clipping safety and exact glyph/cleanup proof. The story remains Backlog during investigation; no production implementation started here.

## Coordinated ownership review - 2026-10-25

Five Luna extra-high reviewers independently examined and discussed the Reconstruction incident. The strongest supported mechanism is the mismatch between persistent raw envelope lifetime and per-frame prepared-caption projection: prepared proof suppresses a local copy, while completed negative scans can expose the same still-live history. This is more directly supported than a stale worker result, selected-output lag, expired hold or renderer reset in the onset interval. Underlying incorrect subtitle detection is not proven without native pixels.

The raw envelope lacks caption/UI provenance, so a blanket clear or coarse no-caption shortcut is not qualified. Next implementation needs a mode-appropriate owner/evidence contract and production handoff replay with final source/target assertions, including dim/sparse UI and unknown-scan controls. See [five-reviewer findings and test plan](../assets/VP-0207/five-luna-review-20261025/review.txt). No implementation began; status remains Backlog.

## Held-envelope production-function reproduction - 2026-10-25

The follow-up [logic investigation and native probe](../assets/VP-0207/held-envelope-logic-20261025/review.txt) reproduces qualified/unqualified/qualified native-expanded-native crop transitions using current beta policy functions. It identifies both retained envelope and dense FIT contributors, expansion surviving translation-confirmation ownership, and a separate skipped-analysis hold-expiry bypass. All probe assertions and expiry/generation controls pass; synthetic proof/routing fixtures are not an end-to-end image replay. No production implementation or deployment; existing story status remains unchanged.

Follow-up [safe protection review](../assets/VP-0207/held-envelope-logic-20261025/protection-review.txt) confirms a label-dependent recovery bypass and that strict clearance scheduling excludes no-bright-ink. Passing current-policy probe controls cover exact/stale UI proof and already-submitted envelopes. Recommends a bounded exact-frame full-row clearance certificate covering both retained contributors, plus geometry-based owner validation. Design only; no production patch or deployment.

2026-10-26: User authorized the narrow exact-frame clearance guard and diagnostic logging. Implementation started on codex/subtitle-rewind-ar-20261025 from current beta81ac960f. Validation in progress; no deployment.

## Narrow clear-row protection implemented - 2026-10-26

Implemented and pushed commit714e35df on codex/subtitle-rewind-ar-20261025. Advanced subtitle modes can reject combined historical vertical expansion only with exact current full-row clearance, current native authority and a matching submitted anchor. Added bounded asynchronous negative-frame scans, cache-safe request identity and diagnostic rejection/success logs. Classic/Off negative-scan behavior is unchanged. Clean x64 Release build passed;1174 focused subtitle/crop/recovery/GPU regressions and4 targeted checks passed (counts overlap). See [implementation, limitations and diagnostics](../assets/VP-0214/clear-row-guard-20261026/implementation.txt) and [validation receipt](../assets/VP-0214/clear-row-guard-20261026/validation.json). No beta merge, deployment or native incident video replay. Broader ownership audit remains In Progress.

## Ben-Hur subtitle and framing regression implementation - 2026-10-26

User authorized implementation and multi-path regression coverage after Astra, Sol and Luna extra-high reviews. Clean source worktree E:\codex\videoprocessor\benhur-subtitle-framing-fix-20261026, branch codex/benhur-subtitle-framing-fix-20261026, starts at freshly fetched current beta 53f7e234d7b4a607322e03295ce1cac8b9bbb6c4.

Replay proves successful submitted framing alternates between 376-2056 and 388-1776 while native picture remains 388-1772. Separate cue-context and rounded-backing cleanup failures occur before current-pixel inspection. Required reproduction covers raw-positive opposite-edge loss, independently measured 384-1776 versus retained 388-1772, and backing-only overlap with glyphs wholly in the bar. Implementation will preserve exact source identity and current pixel authority, test final submission/anchor recovery, and add precise diagnostic reasons. No blanket tolerance increase or owner whitelist. Native footage at the affected transitions is not available; pixel-dependent behavior requires representative validation. Release build, focused/broad regression results and deployment status will be recorded when available. No deployment or active configuration change has been made.

## Ben-Hur regression fix validated - 2026-10-26

Implemented and pushed final source `0f807d43051036a6f25fc75ea4a7e43c36a909f3` in [draft PR #210](https://github.com/billslack2/videoprocessor/pull/210), based on freshly verified current beta `53f7e234`. Astra, Sol and Luna extra-high reviews covered the production policy and the combined subtitle pipeline. Exact current presentation ownership now survives refinement/admission/selected-output/successful-submit handoff. Fresh bar-only glyph proof can survive an unavailable opposite edge without granting AR or cleanup authority; a current-positive sequence can continue beyond the missing-detector grace. Backing-only intrusion is independently re-proved from current cue-side raw evidence and finite native pixels, preserving rounded live corners.

Full solution x64 Release build passed. Final verification passed **3,312 native regressions plus 4 targeted GPU tests** (3,316 distinct tests; earlier overlapping runs are excluded). Coverage includes the logged 376-2056 / 388-1776 submitted framing alternation, independently measured 384-1776 versus native 388-1772, actual P010/P210/decoded Dolby Vision worker-to-composition-to-cleanup paths, more than one second of raw-positive opposite-edge loss, both bars, changed glyph/backing pixels, stale/missing proofs, source/epoch/gap resets, failed/replayed submissions, late cold-caption proof and genuine native/translation controls. A one-row backing contour remains fail-closed; 2-16-pixel finite contours and rounded-corner preservation are exercised.

Broader runs also exposed two older label-only authority fixtures and a baseline GPU probe placed inside the intentional display overhang. The authority tests now require real current evidence and explicitly reject relabeling; the GPU test positively checks the overhang and verifies unchanged pixels beyond it. The original GPU failure reproduced on clean beta; no shader/geometry behavior was changed for that test. [Implementation and limits](../assets/VP-0214/benhur-subtitle-framing-fix-20261026/implementation.txt) and [pinned build/test/hash receipt](../assets/VP-0214/benhur-subtitle-framing-fix-20261026/validation.json).

No beta merge, runtime deployment or active configuration edit. Native incident captures precede the affected transitions, so live Ben-Hur scene acceptance remains pending. The wider story stays In Progress.

## Ben-Hur fix deployed - 2026-10-26

At the user's explicit request, deployed tested source `0f807d43051036a6f25fc75ea4a7e43c36a909f3` from the successful x64 Release build to `C:\Videoprocessor\vp`. Replaced the host, standard renderer and beta renderer together, including matching generated runtime sidecars and PDB symbols. All nine installed file hashes match their exact build sources; all 18 existing renderer dependency DLLs match and were retained. Existing display observers and Config editor were retained.

Backup: `C:\Videoprocessor\vp\backup-before-benhur-fix-0f807d43-20261026-174350-103`. It contains every replaced runtime file plus `VideoProcessor.cfg` and `VideoProcessor.state`. Configuration edits: **none**; both active files are byte-identical to their backups. VP was already closed and remains closed; the open Config editor was not modified. [Deployment receipt and exact before/after hashes](../assets/VP-0214/benhur-subtitle-framing-fix-20261026/deployment-receipt.json).

The deployed source passed the full x64 Release solution build, 3,312 native regressions and 4 targeted GPU checks. Draft PR #210 remains unmerged. Live Ben-Hur replay with subtitles enabled is still pending; this record confirms deployment and hashes, not visual acceptance. The wider story remains In Progress.


## Ben-Hur live failure reproduced and follow-up deployed - 2026-10-26

The first deployed patch failed live acceptance. New native capture2 contains the complete lower-bar two-line caption while VP reports no text. Sampled inputs are byte-identical; the worker repeatedly completes scans with no shared inspection bounds. A fresh native replay finds no caption without the reference and both lines with it. This proves an inspection-starvation loop after the Classic presentation hold expires. The recorded main Scope/Reconstruction pass kept its submitted source crop stable; changing translation metadata alone is not evidence of an uneven transfer or an output AR transition.

Follow-up source `0f39d53007fff8f88f4379dc98e93cd918b3b313` is pushed to [draft PR #210](https://github.com/billslack2/videoprocessor/pull/210). It isolates moving picture pixels from the bar-only continuation veto, bridges cached opposite-plane jitter without changing glyph geometry, accepts separately current boundary proof before strict rounded-body cleanup, and separates inspection nomination from presentation authority. Source/context/revision and worker-cache controls require a fresh scan. Candidate-used observations cannot grant AR, CURRENT_CAPTION FIT, semantic/prepared retention or caption-absence release. Fresh contour acquisition still requires current owned unheld glyphs and full strict body/flank/closure proof; held local continuity needs an existing bounded contour. Four large renderer previews moved to scoped heap storage; compiled stack analysis confirms 227,680 bytes removed from RenderLocked.

Clean complete x64 Release build passed. Final verification: **3,320 native regressions plus 4 affected GPU tests**, 3,324 distinct tests. Eight new focused tests are included in that total. Three behavioral regressions fail at their intended assertions on deployed0f807d43 and pass on the follow-up. The exact missing-state native frame passes twelve consecutive real sampler-to-tracker-to-composition-to-rounded-lip cleanup updates with actual shared crop authority unavailable. [Implementation and limits](../assets/VP-0214/benhur-live-failure-20261026/implementation.txt), [pinned validation and native evidence](../assets/VP-0214/benhur-live-failure-20261026/validation.json), and [runtime nomination eligibility audit](../assets/VP-0214/benhur-live-failure-20261026/runtime-nomination-eligibility.txt). Movie pixels remain outside the repository.

Deployed the matching Release host, standard renderer and beta renderer to C:\Videoprocessor\vp; all nine file hashes match and all 18 retained dependency DLLs match. Backup: C:\Videoprocessor\vp\backup-before-benhur-recovery-0f39d530-20261026-185612-659. VP restarted as PID81912 and host/renderer startup logs confirm0f39d530/dirty0 with active capture/output. Main configuration is byte-identical; the Config editor was retained. The running session subsequently wrote profile.subtitles=classic to its state file after starting in Reconstruction. No deployment edit to that setting was issued; select Reconstruction for the next acceptance replay. [Deployment receipt](../assets/VP-0214/benhur-live-failure-20261026/deployment-receipt.json). No beta merge. Live moving-scene acceptance remains pending and the story stays In Progress.


## Ben-Hur partial acceptance and rounded-shoulder follow-up - 2026-10-26

The user confirms most prior subtitle/framing behavior is resolved on deployed0f39d530. Remaining Float screenshots show original rounded black shoulders wider than the cleaned center; Extend hides more of them but occasionally exposes the lip. The frozen log corroborates per-frame cleanup withdrawal/reacquisition in unchanged cues14 and28. Cue31 has stable no-attachment rejection and is a separate case. This is partial live acceptance; the story remains In Progress.

Astra, Sol and Luna reviews identify the rounded-row common-intersection rectangle as incomplete coverage. Current native dark-neutral row spans now carry separate cleanup authority, preserving the conservative rectangle for existing framing contracts and preserving live rounded corners. Edge revalidation trims only contiguous bounded unsafe endpoints; interior/disconnected changes still reject. The measured envelope is donor exclusion/storage only. Temporal restoration is being made exact to the same row spans, and prepared erasure validates each span against the physical base. Added mismatch-pixel/coverage telemetry will distinguish remaining live contradictions without collecting large frame sequences.

Build and native/GPU qualification are in progress. The first build caught a C++14 compatibility issue in the allocation-free shader parameter lookup; corrected before requalification. No new deployment or beta merge has occurred at this point. Final commit, tests, hashes and deployment receipt will be appended after qualification. Native movie frames and screenshots remain local.


## Rounded backing spans validated and deployed - 2026-10-26

Follow-up source `376e46a3c2d794745e6e04a525b73ca63248b370` is pushed to [draft PR #210](https://github.com/billslack2/videoprocessor/pull/210). Exact-current dark-neutral row masks now clean the proved rounded shoulders while preserving live corners. Bounded edge changes trim only unsafe endpoints; interior/disconnected changes, stale contexts and budget failure still reject. The conservative rectangle remains the framing contract. Immediate and retained temporal restoration use the same validated/coalesced rows; the measured envelope is donor exclusion/storage only. Prepared erasure checks every span against the physical base. New bounded logs identify coverage and the exact first contradictory native pixel.

Actual GPU regressions exposed an additional layer alpha-contract error: libplacebo's internal MAIN hook expects premultiplied signal RGB, while the layer returned straight color. Both ordinary and glyph-only internal returns now honor that contract; the public layer stays independent alpha. The strict direct-versus-split and live-corner assertions pass without tolerance changes. A preliminary native crash was a test-fixture stack overflow, diagnosed by dump/TEB/PDB; three oversized fixtures moved to heap storage with their assertions intact. Production stack reserves and active settings are unchanged.

Clean complete x64 Release build passed. Final qualification: **3,328 native tests plus 10 distinct affected GPU tests**, 3,338 distinct passes. Includes Float/Extend, both bars, black/reconstruction, rounded shoulders/live corners, full/split composition, both temporal donor coordinate paths, transparent styles/transfer formats, native P010/P210/decoded Dolby Vision, malformed/current/stale authority, exact budget, edge trim/recovery and physical-base containment. The exact captured input passes twelve successive real pipeline updates. Cue/phase shader-cache reuse also passes. Compiled renderer and repaired-fixture stack audits match final binary/PDB hashes. [Implementation and limits](../assets/VP-0214/benhur-rounded-lip-20261026/implementation.txt), [pinned validation](../assets/VP-0214/benhur-rounded-lip-20261026/validation.json), and [alpha-contract evidence](../assets/VP-0214/benhur-rounded-lip-20261026/layer-alpha-contract-evidence.txt). Movie pixels/screenshots/dumps remain local.

Deployed the matching Release host, standard renderer and beta renderer to `C:\Videoprocessor\vp`. All nine installed file hashes match; all 18 retained dependency DLLs match. Backup: `C:\Videoprocessor\vp\backup-before-benhur-rounded-376e46a3-20261026-195433-368`. Configuration edits: none; both cfg and state are byte-identical to their backups. VP was closed normally through its keyboard handler at 19:36:17 and remains closed. Config editor retained. [Deployment receipt](../assets/VP-0214/benhur-rounded-lip-20261026/deployment-receipt.json). No beta merge. The story remains In Progress for the user's live moving-scene acceptance; the new telemetry will identify any remaining true pixel contradiction.


## Ben-Hur follow-up accepted and merged - 2026-10-26

User replay feedback: "better. merge this." Merged [PR #210](https://github.com/billslack2/videoprocessor/pull/210) into `v1.4.02-beta` using the vp-release pinned-head/base workflow. Exact tested head `376e46a3c2d794745e6e04a525b73ca63248b370`, unchanged integration base `53f7e234d7b4a607322e03295ce1cac8b9bbb6c4`, merge commit `37b8720a46f88181cc41c283a35f0a2bc18cfbe8`. GitHub confirms merged at 2026-10-27 00:07:53 UTC (October 26 local time).

The actual merge has those two parents and its source tree is identical to the clean accepted feature tree `4272680d1c58d0e3a7071b498afc955b061e6580`. Final x64 Release qualification remains 3,328 native plus 10 affected GPU passes, 3,338 distinct. Merge readiness and check gates passed; no remote checks were pending or failing. The existing qualified deployment is retained. [Pinned merge and acceptance receipt](../assets/VP-0214/benhur-rounded-lip-20261026/merge-receipt.json). This accepts the narrow follow-up for integration; the wider story remains In Progress.

## Dark-frame subtitle continuation follow-up qualified, not deployed — 2026-10-27

The paused Ben-Hur native capture has unchanged glyph/lip pixels across its first and last frames. Logs identify shared-picture authority becoming unavailable during same-cue recovery, and a separate fullscreen renderer replacement losing retained AR history. Diagnostic 384..1776 picture bounds permit the real native pipeline to find the caption; glyph detection alone cannot validate the aspect ratio. No production 2.76 override was added.

Implemented in isolated branch `codex/dark-frame-subtitle-review-20261027`, based on current beta `52f625c113191511f2890775fe9bdd8e511a7049`: narrowly scoped current-pixel local bar-cue continuity without shared picture authority; coordinate-only backing outline retention across uninspected gaps; current source-strip validation around small edge changes; continuation/profile/hold, candidate rejection and native-anchor reset logging. Seeding requires two current edges and an established cue. Subsequent opposite-edge loss keeps exact cue-side boundary matching. Local continuity cannot grant AR, picture capture or cleanup authority; cleanup always needs fresh proof. A true detection miss retains the bounded 250 ms grace, while positively reverified unchanged glyphs do not expire solely because that timer elapsed.

x64 Release solution build passed. Relevant native qualification: **468/468**, including saved-frame replay and continuity/edge safety regressions. Standard GPU qualification: **206 distinct passing tests** (205 full-run passes plus one corrected test-coordinate oracle passing on focused rerun; production code unchanged). Beta dependency: **206/206** in a full run. New GPU paths cover Clear/Yellow + Float, real bright-picture source padding, and UHD Smooth Motion mixed/repeated lip cleanup. Initial broad edge tolerance was rejected by existing tests and removed. The new padding test initially used the wrong mapping; its corrected shader-equivalent pixel-center calculation explicitly requires nonzero bright picture samples.

[Qualification and artifact hashes](../assets/VP-0214/dark-frame-continuation-20261027/qualification.json), [implementation and limits](../assets/VP-0214/dark-frame-continuation-20261027/implementation-and-limits.txt), and [exact uncommitted source patch](../assets/VP-0214/dark-frame-continuation-20261027/reviewed-source.patch). Source changes remain in the isolated worktree; no source commit, PR, merge or deployment was performed. No configuration edits. Native movie pixels remain local.

Still In Progress: live playback acceptance, genuine cue-side movement, and fullscreen renderer replacement AR-history handoff are not claimed fixed. Next test is a short Clear/Yellow + Float + Smooth Motion replay with event time, using the added state/candidate telemetry before collecting larger pixel sequences. Any subsequent deployment must preserve the running `7059c944` fullscreen/windowed renderer-health fix; it is not in this beta-based patch.

## Dark-frame follow-up deployed and restarted — 2026-10-27

At the user's request, pinned subtitle changes as `81ddeeec` and combined the already-installed fullscreen/windowed health fix `7059c944` without conflicts, producing clean deployment commit `038bddc60fac406522c1f2acf57f7f4cde4452bf`. The feature branch is pushed; no beta merge. Full x64 Release solution build passed after removing corrupt generated incremental linker caches. **495 combined native, presentation-health and focused Clear/Yellow GPU regressions passed**. Prior dual-dependency qualification remains recorded above; the added integration is the unchanged previously deployed presentation-health fix.

Force-closed VP and installed the matching host, standard renderer and beta renderer, symbols and runtime sidecars. All nine installed hashes match the build, and all 18 retained renderer dependency hashes match. Backup: `C:\Videoprocessor\vp\backup-before-dark-frame-subtitle-20261027-224216`. Configuration edits: **none**; cfg and state were unchanged by deployment. Restarted as PID30104 with working directory `C:\Videoprocessor\vp`. Startup logs confirm host and renderer `038bddc6`, dirty0, and capture startup. This verifies deployment/startup, not live visual acceptance.

[Deployment receipt](../assets/VP-0214/dark-frame-continuation-20261027/deployment-receipt.json) and [combined validation](../assets/VP-0214/dark-frame-continuation-20261027/deployment-validation.json). Remaining AR-history handoff and live subtitle/backing transition acceptance remain In Progress.

## Subtitle follow-up merged; RC6 qualification underway — 2026-10-27

At the user's request, merged [PR #221](https://github.com/billslack2/videoprocessor/pull/221) into `v1.4.02-beta`. Tested/deployed head `038bddc60fac406522c1f2acf57f7f4cde4452bf`, pinned base `52f625c113191511f2890775fe9bdd8e511a7049`, merge `3bae9f228173b9b2d024d7c977a87766bcbc02c1`; merge tree matches the tested head. [Merge receipt](../assets/VP-0214/rc6-20261027/merge.json).

Preparing a separate GitHub beta prerelease `1.4-beta-rc6`, display version `1.4.0-rc6`, full Setup and Portable ZIP. Clean release branch `codex/release-1.4-rc6` at `ba66661fdad4ec8353f9eb7c43eb30b0731241c7` retains production application sources exactly matching the merge, carries forward existing packaging fixes, and corrects one outdated test expectation: attached panels already overlap the bar by two source rows, and the test now checks both overlap rows plus the first unaffected row. That focused test passed; final complete-suite qualification is running. The first superseded full run was stopped after diagnosing this single reported assertion failure; it was not exported or published.

Final clean Release packages built. All 225 isolated actual-installer lifecycle checks passed using final managed payload hashes and a separate QA identity. Updater policy/control, portable configuration/transactions and Config update-scope/shutdown checks passed. RC6 update metadata is signed and verified. No release is published yet; the complete suite remains the gate. Live installation remains unchanged from the prior deployment.

## RC6 beta published — 2026-10-28

Published [VideoProcessor 1.4 RC6](https://github.com/billslack2/videoprocessor/releases/tag/1.4-beta-rc6) as a separate beta prerelease, tag `1.4-beta-rc6`, version `1.4.0-rc6`, signed updater sequence `20261028024900`. PR #221 remains merged at `3bae9f228173b9b2d024d7c977a87766bcbc02c1`. Release branch/tag source is `e3a34b6c9358485e2d975d7b76c7be1ad05baec2`; production sources match the merged beta exactly. The release branch carries the existing packaging fixes and two test-only corrections separating measured source cleanup from the intentional two-row display-panel sampling overlap. Both corrected tests passed in isolation and in the complete suite.

Final x64 Release qualification: **3,481/3,481 tests passed**, followed by installer support/identity, runtime packaging, finished portable configuration, source identity, and payload/export hash checks. **225 isolated actual-installer lifecycle checks passed** on the final payload, with separate QA registration and managed-payload hash matching. Updater policy/control (29/19), portable configuration contract (8), portable update transactions, and Config update-scope/shutdown checks also passed. No live installation or configuration changes were made for this release.

One superseded full run had 3,480 passes and one intermittent `SameSizeEditWithRestoredWriteTimeInvalidatesCache` failure (expected `two`, got cached `one`). The unchanged binaries subsequently passed all five cache tests, 20 focused repetitions, and the final entire suite. The failure is preserved and disclosed in release notes; root cause is not confirmed and reruns are not claimed to fix it. Earlier superseded runs identified the two obsolete display-overlap test expectations. No failed run was exported or published.

Public assets are only the [Setup EXE](https://github.com/billslack2/videoprocessor/releases/download/1.4-beta-rc6/VideoProcessorSetup-1.4.0-rc6-e3a34b6c9358.exe), [Portable ZIP](https://github.com/billslack2/videoprocessor/releases/download/1.4-beta-rc6/VideoProcessor-1.4.0-rc6-e3a34b6c9358-x64-Portable.zip), and signed `vp-update.json`. GitHub asset SHA-256 digests match local files; public tag target, prerelease=true and draft=false were verified. Checksums/manifests/receipts remain internal. Fresh portable users copy `VideoProcessor.cfg.example` to `VideoProcessor.cfg`; upgrades preserve existing settings.

[Release receipt](../assets/VP-0214/rc6-20261027/release-receipt.json), [additional qualification and known failure](../assets/VP-0214/rc6-20261027/additional-qualification.json), [public release verification](../assets/VP-0214/rc6-20261027/published-release.json), and [release notes](../assets/VP-0214/rc6-20261027/release-notes.md). Windows binaries/installer remain unsigned; updater metadata signature was independently verified against the embedded public key. Clean Windows VM and physical display/capture acceptance were not newly qualified. The wider subtitle/AR story remains In Progress; dark-scene transitions and fullscreen AR-history transfer are not claimed completely resolved. VP-0189 status is unchanged.

### RC6 update: prepared NLS profile reuse (2026-10-28)

The original three-file checkout change is patch-identical to [PR #222](https://github.com/billslack2/videoprocessor/pull/222), already merged into beta at `74c67da75afb12d09691850e1fb9b02d31b70a06`. Updated [RC6](https://github.com/billslack2/videoprocessor/releases/tag/1.4-beta-rc6) uses release source/tag `7af23836ae937353edb50a5b1b3f8e9176634480`: application sources match that beta; the release branch carries prior RC6 packaging and two test-only expectation fixes. Release source is pushed as `codex/rc6-nls-release-20261028`.

The clean x64 Release build passed 3,483/3,483 tests, installer support/identity/runtime and payload hash checks, and all 8 portable configuration contract checks plus the finished archive check. The isolated QA identity passed 225 actual-installer checks and removed its registration. Its first attempt failed preflight because the QA support helper retained the production identity; the corrected fixture passed and the failed attempt remains at `E:\codex\videoprocessor\rc6-nls-release-20261028\artifacts\full-installer-qa-nls-7af23836`. The final lifecycle log is under the adjacent `full-installer-qa-nls-7af23836-retry`. This was a development-machine test, not a clean Windows VM or physical display/capture qualification. The earlier intermittent configuration-cache failure remains unresolved; this run passed.

New [Setup EXE](https://github.com/billslack2/videoprocessor/releases/download/1.4-beta-rc6/VideoProcessorSetup-1.4.0-rc6-7af23836ae93.exe) SHA-256 `83a8bc34d1c4ad0ad4de99fa016f6c661ad1b9c77035653c48237167c4c4157c`; [Portable ZIP](https://github.com/billslack2/videoprocessor/releases/download/1.4-beta-rc6/VideoProcessor-1.4.0-rc6-7af23836ae93-x64-Portable.zip) SHA-256 `88dfd9bee7d4018e79e212add673f955f6c8b4500a6ed488c388a9f7871cbec7`. GitHub asset digests matched. Signed updater metadata sequence `20261028143402` was downloaded and signature verified; prior RC6 assets remain for rollback. Installer and binaries are unsigned. Fresh portable users copy the example configuration; upgrades preserve their settings. Evidence and tool inputs are in `assets/VP-0214/rc6-nls-20261028/release-receipt.json` and the complete local run at `E:\codex\videoprocessor\rc6-nls-release-20261028\artifacts\release-verification-a99a80988c8541c4811913c16f075c8c`.
