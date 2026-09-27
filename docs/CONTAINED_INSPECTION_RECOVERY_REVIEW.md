# Contained inspection recovery correction

## Observed problem

After a legitimate taller presentation, an expired inspection could indefinitely block return to an already admitted scope crop. In the paused Eternals report, scope remained saved at0,276-3840,1884 while output used0,34-3840,2126. The current provisional observation788,320-3840,1884 was wholly contained; saved top/bottom bars sampled safe and near-black was false. Recovery stayed0/7 under presentation-owner-unresolved. Existing lookahead had four future frames but could not resolve this ownership cycle.

## Narrow correction

CanResolveContainedInspectionRetention clears only that obsolete inspection latch. It requires the same crop contract in inspection, active recovery, previous admission, current geometry and fresh retention evidence, including matching generation/epoch/raster/axes. The immediately preceding admitted presentation must be a valid wider envelope containing that crop. Current evidence must be available provisional contained pixels with safe excluded bands and non-near-black analysis. Current outside visibility, real fit/translation owners, moving transitions, full-raster authority and stale evidence reject the path.

It grants no new crop authority and does not set sampling equivalence. The renderer preserves independent vertical fail-open and invalid-expansion causes. Existing recovery keeps the wider picture while collecting its normal proof; at24fps the reproduced sequence requires seven safe frames. An unsafe frame resets all accumulated votes. The previous partial-outward strip correction, acquisition thresholds, lookahead and caption policies are unchanged.

Regular source-crop diagnostics add contained_inspection_resolved, vertical_fail_open and invalid_expansion, including deduplication keys. Recovery logs also identify contained inspection resolution. There are no additional source-pixel scans or configuration settings.

## Tests and review

- RED: clean x64 Release build; one intended failure in valid latch resolution, two unsafe-case tests passed. The test first confirms the existing pipeline remains at0/7 with RECOVERY_OWNER before checking the missing resolution.
- GREEN: four new tests passed unchanged after implementation, including48 negative context/authority cases and actual synthetic OSD/taller-picture pixels.
- A fourth test inserts unsafe pixels after three safe votes, verifies reset to zero and retention of the wider picture, and requires seven fresh subsequent votes.
- Full native suite: **1,731 passed, zero failed**. Existing assertions unchanged.
- Independent architecture, renderer-integration and policy/test reviews found no remaining blocker. Existing captions/OSD, desk false acquisition, gradual fades, lookahead and genuine AR-transition tests passed.
- An initial incremental RED build failed with MSVC LNK1103 corrupt debug information before tests ran. It is preserved; clean RED and GREEN rebuilds succeeded.

Receipts: artifacts/partial-retention-proof/contained-red/build.log (toolchain failure), contained-red-clean/tests.trx, contained-green/build.log and tests.trx, contained-full/tests.trx. Source and built Release binary hashes: artifacts/contained-inspection-proof/validation.json. Source remains uncommitted on beta baseab683eefe5fdffa018b788eedfcd8913dbebe1b4; that base SHA alone does not identify these binaries.

## Deployment and remaining validation

This correction has NOT been deployed or merged. The previously deployed partial-outward build remains unchanged. No runtime configuration edits were made.

Tests use synthetic source pixels and the logged policy sequence, not a recording of the exact Eternals HDMI frames. Next viewing test: replay scope-to-taller-to-scope, pause on the reported dark frame, verify contained_inspection_resolved=1 and recovery advancing instead of staying0/7. Repeat with playback controls/captions and a gradual transition. Initial ambiguous crop acquisition and full-raster fallback without continuously admitted wider framing are intentionally outside this correction.
