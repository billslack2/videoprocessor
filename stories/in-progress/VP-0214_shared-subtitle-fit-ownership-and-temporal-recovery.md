# VP-0214: Audit shared subtitle fit ownership and temporal recovery

## Status

In Progress

2026-10-25: Follow-up concern identified during the independent team assessment
for VP-0213. Investigation/implementation of shared subtitle behavior is not
part of VP-0213; no Classic/reconstruction policy change is made by that repair.

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
