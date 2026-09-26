# Qualified remembered-edge return experiment

This opt-in return path recognizes an independently established full-width vertical crop when one native edge recurs precisely but dark picture causes the opposite raw scan to over-inset. It does not discover new formats, relax SameBounds, or treat windowboxes as full-width programs.

## Prior and current evidence

A separate bounded registry stores native rectangles only after exact accepted/native observations. Three fresh verifications within each of two confirmed scenes are required. A scene ID comes from confirmed scene cuts, not every candidate reset. Experimental/remembered publications cannot train the registry. Source/renderer/source-format changes invalidate it. Source-compatible presentation/profile resets preserve qualified rectangles but clear current stable/candidate authority; viewport/policy changes invalidate pending proofs, not the independently learned source facts. Entries expire after ten minutes or64 confirmed scenes without independent verification.

Exactly one native trusted top/bottom edge must match one qualified entry's exact coordinate. Multiple candidates abstain. The target must be materially inward from the current native full-width base. The raw observation must have completed, conflict-free horizontal scanning and vertical asymmetry with the opposite scan too far inward.

The currently trusted bar alone defines a reference profile (8 depths x96 columns, omitting its innermost8 rows). Median Y must be <=80 in normalized10-bit analysis units; Y/U/V p90-p10 must each be <=4, and all reference samples must lie within4 codes of their medians. Neutral chroma medians are bounded to512+/-8. Positive picture evidence at the matched edge must be distributed; positive contrast at the missing edge is intentionally unnecessary.

Both exact prospective excluded bands are sampled at96 horizontal positions. Rows include every one of the8 immediately excluded rows at each picture edge,8 rows at each raster boundary, and every detector-step row throughout each bar (`max(1, height/540)` pixels; duplicates removed). Sampled Y/U/V values must remain within4 codes of the clean reference, with one narrowly bounded exception: at the independently matched edge only, chroma may differ by up to12 codes while luma must still remain within4. This covers only the immediately excluded decoded chroma row (one luma row for P210/native422; only excluded rows sharing that chroma pair for P010). The opposite bar and all deeper rows remain strict. This tolerance does not prove the signal is filter bleed: an authored nearly black colored line inside that tiny footprint is indistinguishable. It cannot change the remembered rectangle or bypass confirmation, captions, side conflicts, or retention checks. Neither the uncertain bottom nor remembered old black measurements can widen the current reference. Ordinary current pixel-retention and outward-content checks also apply. Sampling is bounded, not an exhaustive proof against arbitrary artwork or unsampled content.

## Ownership and timing

Successful inspection supplies separate REMEMBERED_EDGE_RETURN provenance and a typed proof bound to history ID/revision, exact current native base/target, source generation/sequence, scene, and context timestamp. Raw proposed bounds and axis failures remain available. Only the specific vertical asymmetry is bypassed after proof validation; side conflicts and normal near-black/caption/movement/admission protections remain.

Two consecutive qualified source frames are required. Repeats, gaps, contradictory frames, cuts, or context changes cannot combine proof. Pending evidence does not revoke the current full-raster/presentation owner. Inferred geometry cannot masquerade as native history; exact independent native verification can subsequently replace it.

This first experiment is live-only. Native queued lookahead remains unchanged; no future-frame certificate is manufactured for this inferred origin. The additional profile inspector runs only for a qualified returning candidate. While opted in and eligible, raw extraction runs on every fresh source frame to learn consecutive native confirmations and support prompt return; this is additional CPU work compared with the normal analysis cadence. Disabled behavior is unchanged. Sample telemetry includes the additional existing retention inspection, not only the profile grid.

## Enablement and validation

Set before launch: VP_REMEMBERED_EDGE_RETURN=experimental. The option is read at process startup and applies to returning formats during playback. No configuration file edits are required. Diagnostics report qualifying history and candidate outcomes, source identity, current reference profile, excluded mismatches, sample count, and timing. Default behavior is unchanged while disabled.

Key tests include zero opposite-edge contrast after qualified history; Y80 displaced ground/ramp against Y64 bars; one/two-row picture intrusions; chroma/noise mismatches; unknown, ambiguous, stale or inferred history; repeated and skipped frames; caption/side conflicts; and pending-publication ownership. Test artifacts are under artifacts/remembered-edge-return. Live playback remains necessary after synthetic validation.




Candidate diagnostic schema2 reports tolerated fringe sample count and row range, plus the last rejected row. Global maximum Y/UV deltas include tolerated samples; `mismatches` counts only rejected samples. The refinement and its tests are recorded in `artifacts/remembered-edge-fringe`.
