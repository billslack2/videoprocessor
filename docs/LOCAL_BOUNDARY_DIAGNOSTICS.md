# Local boundary diagnostics (experimental)

This probe measures sparse picture detail near possible top and bottom boundaries. It is an opt-in experiment for scenes such as the Alien opening: yellow detail can end at a repeatable boundary while the existing whole-row test still has insufficient contrast.

**It does not change crop detection, crop retention, NLS, subtitle handling, lookahead, or presentation geometry.** Every observation, experimental hypothesis and retained anchor is explicitly ambiguous. Experimental eligibility is not confidence or proof of a picture boundary; no diagnostic result connects to crop policy.

## Enabling a capture

Set `VP_LOCAL_BOUNDARY_TRACE=shadow` in the environment of a newly started VP process. It is a startup-only, process-local option; no configuration file change is required. Leave the variable unset for ordinary viewing. The option is case-sensitive and `shadow` is the only enabling value.

Schema 2 logs `Alpha local-boundary`, `Alpha local-boundary edge`, `Alpha local-boundary hypothesis` and `Alpha local-boundary anchor` records at most every 200 ms, for 600 observations per renderer instance (about two minutes at full sampling rate). Rendering cadence can make the actual interval longer. A valid CPU analysis source must already exist; the probe does not request a GPU readback, conversion, new lookahead frame, or queue delay. An exception disables the probe for that instance.

The initial log includes an instance identifier and `policy_effect=none`. The regular log records the source sequence, generation/format/policy/continuity identity, interval bounds, sample count, sampled exterior violations, edge coverage, history reset reason, and inspection duration. Hypothesis and anchor records also include the exact monotonic `tick_ms` used by the experiment, scene generation and duplicate-frame status. No raw images are retained by the rolling history.

## What is measured

The shared source sampler reads at most 128 columns by 270 rows in source coordinates, including the endpoints. It supports existing native and converted analysis sources (including native v210, P010 and P210). It uses decoded ten-bit Y/U/V code values; these are not Photoshop RGB values. Precision metadata describes the originating encoding. Unknown or eight-bit origin is never reported as known high precision.

A row has local support if at least three adjacent **grid samples** exceed either a luma cutoff (perimeter-derived floor + 24, capped at 104) or a chroma distance of 8 from neutral 512. This is a permissive diagnostic selection, not a revised production threshold. Three samples are spaced tens of source pixels apart at UHD: they do not establish a contiguous source-pixel edge.

The first and last supported rows define a coarse interval. `bottom` is exclusive. `uncertainty_y` is the maximum sampled row spacing, not an exact boundary error guarantee. The probe reports luma-only, chroma-only and joint support; same-column Y/U/V changes relative to the adjacent exterior row; and same-column persistence in the next three sampled inward rows. The support mask is 128 bits; the rightmost bit is column zero, with unused upper bits zero for smaller grids.

Exterior violations are measured separately using luma above the diagnostic cutoff or chroma distance above 32. `sampled_bars_clean` requires sampled rows above AND below the interval and no such violations. It means only those sampled locations are clean. It does not certify every pixel or prove those regions are authored black bars.

## Temporal measurements and safeguards

Candidate-discovery history retains masks, fingerprints, source sequences and timestamps for at most 32 observations over three seconds. It resets on invalid input, missing candidates, sampled exterior violations, geometry drift beyond the original anchor's sample spacing, context changes, backwards time/sequences, or a sample gap over 500 ms. Boundary tolerance is anchored; repeated small shifts cannot walk the anchor. The renderer also resets on source-sequence discontinuities and **observed** scene cuts between sample ticks.

Scene cuts depend on the existing scene detector. If it is not running, unreported cuts cannot be guaranteed to reset this probe. Renderer continuity and scene-cut resets clear both discovery and any retained anchor; a rejected new candidate alone only clears discovery. For session source/context invalidation or anchor expiry, that observation cannot immediately acquire a replacement; subsequent eligible observations start a new window.

Repeated polling of the same source sequence cannot refresh evidence age. Different source sequences with identical sampled pixels count as observations, but only one distinct fingerprint. Noise or unrelated interior motion can change a fingerprint; `distinct_sampled_frames` does not mean independent edge witnesses. Coverage accumulated at different horizontal positions is also not proof of independent objects.

A moving yellow object on an intentionally black full-frame background can produce the same spatial and temporal pattern as a genuine letterboxed picture. The adversarial test preserves that ambiguity instead of asserting an impossible semantic distinction. Production crop decisions remain unchanged for both interpretations.

## Experimental acquisition hypothesis and fixed anchor (schema 2)

The following gates are an offline/diagnostic hypothesis, **not established safe production thresholds**. Every frame in the admission window must have supported source precision, sampled clean bars on both sides, at least three connected support samples on each edge, inward continuity of at least 0.75, and opposing inset differences within twice the maximum row spacing. The window needs at least four fresh source sequences over 600 ms. One edge must cover at least 25% of sampled columns cumulatively and the other at least 10%. The latter edge must have at least three different support masks and add at least 6% of sampled columns relative to its first mask. Fingerprint changes alone do not qualify.

A qualifying hypothesis creates a **diagnostic anchor** at the current sampled boundary. It freezes the top/bottom grid rows, luma floor and cutoff, creation time and identifier. It never moves a displayed pixel. Each observation samples the source grid once; the same grid serves candidate discovery and validation of every sampled row outside the fixed anchor. A weak or missing new candidate clears discovery history but does not by itself erase the independently checked anchor.

Anchor telemetry distinguishes a failure outside the **new proposal** from a failure outside the **original anchor**. It records separate top/bottom sample and violation counts, peak Y, Y above the frozen floor, and maximum chroma distance. For this capture, an anchor at `top=280, bottom=1879` checks the adjacent exterior samples at Y=272 and Y=1886. Rows 280 and 1878 are picture-edge witnesses. Physical rows 273..279 and 1879..1885 are unsampled; the log states these gaps rather than claiming they are known black.

Any measured violation of the original anchor exterior releases it. Invalid input/precision, identity or grid changes, observed cuts, backwards time/sequences and gaps over 500 ms also clear it. Its hard, non-renewable lifetime is 15 seconds. Expiry clears admission evidence and forbids replacement on that same observation. This limit is a diagnostic observation window, not a proposed crop hold or permission to ignore scene changes. Same-sequence polling and cadence repeats cannot qualify or renew it.

Symmetry, motion and clean sampled margins still cannot distinguish every moving object from authored bars. The adversarial moving-object test intentionally qualifies the experimental hypothesis while retaining `ambiguous=1` and `policy_effect=none`. Production use would require evidence beyond passing this experiment.

## Validation and next decision

Tests use raw source buffers and adversarial patterns, including native v210/P010/P210 equivalence, sampled obstructions, repeated pictures, source identity changes, drift, aspect ramps and non-mutation of source pixels and existing detector results. Existing production tests are unchanged.

The earlier Alien raw-grid capture is three seconds apart. It can verify spatial observations in the real helper, but its timestamps must cause `sample-gap` resets. It cannot validate a continuous five-Hz history. Positive temporal testing on those grids with shorter timestamps would be a simulation and must be labelled as such.

Before any production authority is proposed, capture the actual moving scene at the new cadence and compare it with moving objects, titles, bright interiors with dark borders, gradual AR changes, subtitles and overlays. The test evidence must justify a constrained rule that improves useful acquisition without weakening existing protections. The current probe itself promises no detection improvement.

## Initial validation: 2026-09-24

Base: beta `ff53b4683a2a6b59423f05918e29ce1e0c8cdca8`, isolated branch `codex/alien-local-edge-review`.

- Native Release RED: temporarily substituting an empty helper produced 17 failing new tests and the one expected passing non-mutation test. The real source was restored byte-for-byte. An initial incremental rebuild retained stale stub objects because the restored file had its original timestamp; that run was rejected as validation. A clean solution rebuild then compiled the real helper.
- Native Release GREEN: all 18 new tests passed without expectation changes. The full existing-plus-new native suite passed **1,489/1,489**. The x64 Release solution rebuild completed with zero errors; warnings were in existing unrelated files.
- Independent implementation, QA and safety reviews found no blocker for diagnostic-only use. This is not approval to use these measurements as crop authority.
- Native replay through the real compiled helper, using the original Alien timestamps: six clean moving samples all measured coarse `top=280`, `bottom=1879` (exclusive), with maximum row spacing 9. Top coverage increased from 20 to 70 grid samples; bottom coverage stayed sparse at 3-4 samples. All six had zero sampled exterior violations. The obstructed sample had 17 violations and cleared history. The three-second inter-sample gaps reset each clean history window as intended; temporal accumulation has not been validated on this real clip.
- Local 4K microbenchmark, 100 observations per format after warmup: median/P95/max milliseconds were P010 **0.860/1.170/1.252**, P210 **0.833/1.038/1.196**, native v210 **0.885/1.098/1.312**. Each read 34,560 samples. This measures sampling and analysis on synthetic source buffers, not renderer scheduling, logging, or overall playback performance.

That first prototype was subsequently deployed and replayed. The actual five-Hz capture contains 106 consecutive observations over about 21.9 seconds with coarse bounds 280..1879 and zero sampled exterior violations. The top and bottom support masks changed across the moving interval. A paused interval also changed fingerprints despite almost fixed edge masks, confirming why fingerprints alone must not supply corroboration. Clean but asymmetric 48..2047 and 208..1879 proposals were present as negative controls.

Later inward proposals do not establish that the original exterior became unsafe: the original capture did not separately report that measurement. Schema 2 addresses that instrumentation gap. Feature-only replay can evaluate admission using recorded masks but cannot reconstruct unrecorded source pixels or validate retained exteriors. Original schema-1 timestamps have whole-second precision; replay timings derived from source sequence and capture rate must be labelled approximate. Schema 1 also lacks the cadence-repeat flag: feature replay assumes false while still deduplicating accepted source sequences, so it cannot exactly reproduce schema 2's freshness gate.

The next live validation is a fresh moving capture with schema 2. Until then, no real-clip fixed-anchor result is claimed. Crop authority remains unchanged.


## Fixed-anchor diagnostic validation: 2026-09-24

- Added 18 separate anchor tests; the original 18 diagnostic tests remain byte-for-byte unchanged. Replacing only the new API with no-op stubs caused 17 failures; the duplicate-frame abstention test and all 18 older tests passed. The real source was restored with matching SHA256 before the clean Release rebuild.
- A first GREEN attempt exposed a fixture error in the cutoff-drift test: its unchanged side perimeter correctly kept the fresh cutoff at 88. Raising every neutral background sample made the intended fresh cutoff 104; the original-anchor cutoff assertion stayed 88 and every release assertion remained unchanged. Repeated RED/GREEN then validated the corrected fixture.
- Final x64 Release rebuild: zero errors, 43 warnings in existing unrelated code. Targeted **36/36** and full native suite **1,507/1,507** passed. Tests cover source-format parity, retained-anchor non-mutation of pixels and existing detector outputs, discovery versus original-exterior conflicts, adjacent sampled rows, fixed cutoff, expiry without renewal, duplicate frames, invalidation and ineligible historical evidence.
- Native feature-only replay through the real compiled helper: 89 overlapping windows qualify in the final 600-observation capture, all at coarse bounds 280..1879, logged 19:17:39�19:17:58. The first qualifies at sequence 2006 with top/bottom cumulative support 32/128 and 16/128. The two negative shapes (48..2047 and 208..1879) never qualify. Across all 913 observations and seven renderer instances, these same 89 windows are the only positives.
- Replay timing is approximate, derived from source sequence and each instance's logged input rate. The 377 records with nonzero fresh-candidate exclusions lack per-edge splits and are conservatively rejected as incomplete metadata. These windows neither prove safe crop authority nor establish one continuously retained anchor; their span exceeds the diagnostic anchor's hard 15-second lifetime. Only a fresh schema-2 capture can show its actual lifecycle and original-exterior measurements.
- A synthetic 4K microbenchmark exercised the full source-sampling session, with an active anchor in 96/100 timed observations per format after five warmups. Median/P95/max milliseconds: P010 **1.161/1.985/2.631**, P210 **0.934/1.184/1.647**, native v210 **1.309/1.983/2.305**. All used 34,560 source samples and matched source fingerprints. These are local CPU measurements, excluding logging and renderer scheduling; they are not end-to-end playback guarantees.

Validation outputs and binary/source hashes are recorded under `artifacts/local-boundary-anchor/`. The completed step changes opt-in diagnostics only. It is ready for a new live diagnostic capture; it is not a production crop fix.

## Independent side-edge diagnostics (schema 1)

Regular logs now inspect both actual source edges when native evidence has clean,
full-width top/bottom bars but rejects its horizontal bar proposal. This diagnoses
a gap where the rough side scan proposes an inset and the authoritative side
picture minimum remains -1 (unmeasured). The new results never populate that
authoritative minimum, observations, or model decisions.

`Alpha side-probe diagnostic` identifies renderer instance, source generation,
sequence, proposed rectangle, exact trusted vertical aperture, existing and
diagnostic minima, sample count, and inspection time. `spatial_pass` means the
new measurements meet the existing distributed-picture threshold; it is not a
crop decision. Other axis conditions (including asymmetry) can still reject it.
`policy_effect=none` is explicit.

The two zone lines describe 12 cells per edge (three depths by four vertical
zones), each with 12 points: strong count, nonblack count, mean Y, and peak Y.
Thresholds and units are recorded in normalized 10-bit analysis luma. Exactly
288 source luma samples are read per eligible inspection, with the same P010,
P210, and native v210 sampling semantics as detection. Invalid source/aperture
contexts abstain. This adds no new chroma or color rule.

Only live evidence is inspected, every two seconds while unchanged and at most
ten times per second on classification/admission changes. New source generations
receive an immediate sample. Queued lookahead does not run these diagnostics.

Periodic diagnostics can miss short blocked intervals. Use the existing per-event
admission logs for timing; these samples describe only their recorded sequence.

## Opposing-boundary diagnostics

For provisional native full-width observations rejected for top/bottom asymmetry,
a shallow trusted edge may nominate a matching opposite location. This diagnostic
remeasures both nominated edges with the unchanged native predicates and compares
48 paired samples immediately inside and outside each edge. It never modifies
crop evidence, authority, admission, or publication.

`Alpha opposing-boundary diagnostic` records raw and nominated rectangles,
whole-bar contrast, adjacent-row means/delta, outside p90/dispersion, and paired
support counts. `Alpha opposing-boundary edge` records exact row coordinates,
original native predicate values, and the required per-pair delta. Support is a
count only; no new crop acceptance threshold exists. `policy_effect=none` applies
even when `strict_pair=1`: dark ground extending past a nominated edge can pass
whole-bar predicates without having a real boundary there.

Successful measurement requires exactly 864 luma and 576 chroma reads. P010,
P210, and v210 use the existing source decoders. Failed/unsupported sampling
abstains. This shares the live-only throttling above and adds no queued work.
