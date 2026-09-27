# Presentation envelope lifetime review â€” 2026-09-27

## Report and evidence

Continuation of the currently tested build in `codex/partial-retention-proof-20260926` at base `ab683eefe5fdffa018b788eedfcd8913dbebe1b4`, preserving earlier uncommitted fixes. The reported TS5 event is in the session starting 00:28:58, saved at `artifacts/presentation-envelope-lifetime/reported-session-002858.log`. The later 08:05 session is not used as evidence for the reported event.

At 00:44:09 (sequence 21645) and 00:44:20 (21903), logical crop remained `(0,44)-(3840,2116)`. Current outward evidence was `(0,28)-(3840,2132)`, but presentation selected `(0,0)-(3840,2132)`. The top-zero extreme originated around 00:31:53. Fresh nearer content refreshed one global timer and therefore preserved that older extreme indefinitely. At 00:44:18, loss of simultaneous current two-edge expansion returned to the logical crop, then FIT re-engaged. Wider-content fill was off and `fill_applied=0`; this was not the user's 1.78-to-1.85 fill setting. The logical-crop deadband does not govern bounded outward presentation.

The 00:44:07 event also ends a long recovery episode. This patch does not claim to explain or eliminate every recovery transition. In particular, a recovery-restored FIT is not allowed to create the new hold certificate.

## Corrections

- Each retained outer extent has its own original detection time. Fresh nearer content, or activity on a different edge, cannot renew an old farther extreme. Existing configured release duration is reused.
- Generic vertical FIT composes from the current approved vertical bounds, not an accumulated historical vertical maximum. An exact already-admitted generic FIT may bridge a short evidence gap only with current native crop/retention identity and covered current pixels or verified safe bands. A held frame never refreshes the admission time. Changed geometry, generation, viewport epoch, competing presentation owner, unsafe pixels, darkness, or motion retires it.
- Dense vertical FIT had the same min/max plus global-refresh error. Its two extents now expire independently. Backward/expired samples cannot resurrect it. Existing subtitle TRANSLATE ownership and maximum-shift retention remain unchanged.
- A current-frame geometry publication may use current observed bounds across a base change, but never historical extremes accumulated against the old base. Absent or invalid evidence falls back to the effective base.
- Initial viewport epoch zero is a valid comparable identity; changing it invalidates the new certificate.
- Removed the renderer's duplicated historical rectangle/timer members and obsolete max-union implementation, plus unused local expansion flags and a misleading historical comment. Existing unrelated dead API fields were not removed as part of this behavior fix.

No crop thresholds, learned-aspect qualification, aspect deadband, input pixel scans, lookahead depth, or queue synchronization are changed. Guarded learned-aspect default remains on as previously requested. No additional locks, waits, or image analysis are added.

## Review and validation

Three independent collaborators reviewed extent lifetime, renderer/admission ownership, and architecture/regression coverage. Existing test assertions were preserved. The original 11 focused tests produced 8 failures and 3 passes before the corrections (`artifacts/partial-retention-proof/envelope-lifetime-red/tests.trx`). Two selector regression groups and initial-epoch coverage also failed before their corrections (`envelope-review-red-tests/tests.trx`). Additional stale/duplicate-sample negative coverage was added during review.

The first incremental review build successfully linked the new test DLL but encountered renderer LNK1103 (corrupt compiler/linker debug information). The freshly linked test DLL supplied the three expected review failures; final validation requires a clean complete x64 Release rebuild.

Final validation: clean x64 Release rebuild succeeded; all 15 focused tests passed and the full suite passed 1,788/1,788 on its first run. `git diff --check` passed. Comparing the two touched test files with the pre-turn snapshots removed zero nonblank existing lines. The three reviewers found no remaining blocker in the bounded lifetime, current-only selection, or final-admission wiring.

Exact source and binary hashes plus test counters are in `artifacts/presentation-envelope-lifetime/validated-build.json`. The built GUI host maps to deployed `VideoProcessor.exe`; renderer and host must deploy as one pair. No deployment, restart, configuration edit, merge, or packaging occurred in this task. The running deployment remained PID 35632 from 08:05:52 with the prior runtime hashes.

## Next playback validation

Replay the TS5 1.85:1 segment on the same 16:9 screen profile with the same captions/settings. Confirm stable native `(0,44)-(3840,2116)` geometry, no unrelated historical top-zero extent, and `generic_fit_held=1` across short eligible evidence gaps. Verify genuine OSD/caption content outside the admitted rectangle still wins and that ordinary scope/IMAX transitions remain responsive. The tests reproduce source rectangles and timing from logs; they are not captured-frame replay and cannot prove the visual symptom is completely eliminated. No new global ignore threshold was introduced: real occupied pixels still require appropriate presentation handling.
