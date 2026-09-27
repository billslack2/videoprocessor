# German TV false crop: optional fill amplification

## Evidence

Reported build 78a1f61bf1e6d6283fc990e86a32b7a8daf65802 is confirmed for host and renderer. The supplied vp_temporal-acq-p-20260926.txt contains likely matching visible events at 15:03:06-15:03:11 and 15:03:55-15:04:01, about 49 seconds apart. The user could not identify exact times but reports two occurrences. The log proves the state/layout changes; title/scene identity and false nature of pixels cannot be independently reconstructed. This segment is 4K/50 Hz. Other logs and later portions contain other presentations and restarts.

First burst: trusted content (0,176)-(3840,1980), AR2.12860, exceeds configured wider fill limit2.0 on16:9. Expanded presentation (0,94)-(3840,2052), AR1.96118, passes the same limit when incorrectly treated as content. Fill then crops180px from each side; it turns off at3908 and back on at3911. Second burst: trusted (0,168)-(3840,1992), AR2.10526; envelope (0,96)-(3840,2042), AR1.97328; fill removes190px from the left and192px from the right due to even alignment. Relevant exact records and input hash are preserved in this artifact directory.

## Narrow correction

Optional fill now accepts a separate content-reference rectangle. Both reference and actual presentation must pass the same existing configured limit and be on the same side of screen aspect. Reference aspect is computed from valid same-raster coordinates, not cached aspect metadata. Reference can only veto; actual envelope checks and centered/chroma-aligned crop calculation are unchanged. Omitted reference preserves helper caller compatibility; the renderer always supplies it for ordinary optional fill. Renderer uses the same-generation effective content geometry, or full raster when independently authoritative. Missing/stale reference fails closed. Explicit fixed crop, NLS ownership, protected captions and bounded recovery branches are unchanged.

Regular source-crop telemetry adds fill_reference_aspect and includes it in deduplication. No new scans, locks, timers, thresholds, configuration or per-frame log stream.

## Validation and limits

Four new tests ran before the policy implementation: three failed as expected, one preservation case passed. The logged180/190px zooms reproduce under the previous envelope-only calculation. Tests also cover narrower-limit crossing, direction changes, valid ordinary/full-raster fill, no authority, malformed reference and preservation of the actual-envelope limit. Existing test assertions were not changed. Final focused6/6 and full1,813/1,813 pass on a successful x64 Release build. Independent architecture/test and implementation/wiring reviews found no blocker. git diff --check passes. Exact source and binaries are pinned in validated-build.json, and this-change.patch preserves this task's delta.

This prevents the demonstrated optional-fill amplification. It does not establish that the original native bar acquisition was correct or eliminate that false detection. The underlying detector accepted roughly symmetric dark margins. Without current source pixels or replay, tightening edge/texture/coverage rules could regress legitimate dark scenes. No speculative detector change was made. A short clip or source-frame diagnostic sequence around the two events is still needed to safely address the initial false bar classification; a controls-hidden screenshot helps but lacks temporal evidence.

No deployment, configuration edit, merge or packaging occurred. The prepared build retains the previously validated learned-family and presentation fixes. Next validation: deploy only when requested, replay the German scene if available, and inspect fill_reference_aspect together with content_aspect/fill_applied and actual final layouts. Also replay permitted near1.85 fill, scope/IMAX transitions and captions to confirm visual behavior. Automated policy replay is not a substitute for that HDMI test.
