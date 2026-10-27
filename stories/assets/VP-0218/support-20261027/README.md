# VP-0218: Supporting the visibly clear textured ad

Follow-up research, 2026-10-27. Three Luna reviewers and the parent investigated the actual captured pixels, existing code, independent controls and real subtitle captures. No production code, configuration, process or deployed binary changed.

## Conclusion

The user's visual assessment is supported by measurement: this sample has strong, straight, independently detectable upper and lower borders. The detector should be able to use that evidence even when the upper bar is gray and uneven. We have demonstrated boundary recovery and a bounded spatial background model that explains the gradient without using screen geometry or a standard aspect. This is a feasible direction for additional detection evidence, not yet a qualified crop route.

The missing capability is a separate interpretation of the varying exterior background plus protected foreground detail. Current acquisition and visibility checks share one absolute black reference; the raised upper band defeats both. Simply relaxing a component-area veto or replacing raw visibility with a local residual would introduce potential false crops in the control set. The next implementation should keep measured picture geometry, background evidence and preserved foreground/subtitle extent separate.

This continues [the first research pass](../research-20261027/README.md). Current source baseline remains fetched GitHub beta `v1.4.02-beta` at `7394ba420999a8fa708718ee4ce4d87e7459369f`, clean worktree `E:\codex\videoprocessor\vp-0218-boundary-research-20261027`. Diagnostic C++ probes link the existing optimized beta objects. The previously deployed contained patch remains `ddf5d666803c29e0bdf7af310963b2695e382bc2`; no new build or deployment occurred here. The earlier 149 passing existing tests are baseline evidence, not validation of these new hypotheses.

## What is now demonstrated

| Question | Evidence | Result |
| --- | --- | --- |
| Are the borders actually measurable? | Previous independent scans on all 97 saved frames, four unique payloads | Yes: full-width/quartile support, fine top outside/inside 300/301 and bottom inside/outside 1856/1857. No mirrored edge or aspect prior. |
| What stops current VP? | Unchanged native beta extraction, fallback, continuation and retention probes | Upper scan cannot start at cutoff 88. At a supplied near-border hypothesis, top black fraction .503, P90 180 and spread108 fail, despite raw texture 6.894, neutral fraction 1 and contrast +190.8 passing. |
| Is all the flagged upper content truly detailed picture? | Direct original-coordinate diagnostic, 6x48 summary and 64x256 visibility grid | Raw guard-band visibility counts 6426 probes. A local17 reference explains most as the background, but self-fitting can absorb foreground and is not sufficient. |
| Can a separately estimated spatial reference explain the field? | Fixed 12x6 cubic B-spline, checkerboard training/validation tiles | Held-out absolute residual P90 14.24 and signed spread15.02 fit the 24-code summary limits; exact 6x48 summary also passes. Raw texture/chroma remain passing; high residual detail remains. |
| Does a relaxed morphology gate safely solve it? | Rule calibrated above known ad component sizes, then independently challenged | No. It admits the ad but also faint glyphs, stars and a tiny logo. Reject this as standalone safety. |
| Can the sample grid replace caption checks? | Real Thai/Latin captures and nominal-versus-guarded phase comparisons | No. Local referencing and changed sampling phase can weaken caption witnesses. Existing raw retention catches both captions. |

The saved clip is one short repeated scene: 97 distinct saved accepted identities but four unique pixel payloads. Repeated pixels are not automatically repeated source identities, and neither count is a broad scene corpus. These experiments cannot measure improvement in moving-transition latency.

## Background model and residual findings

The upper band has 22 residual components at local17 threshold 12, totaling 1262 pixels (about 0.112% of the guarded band). They are sparse but fixed: the thresholded masks are identical across the four payloads. They are not established transient sensor noise. The largest residual reaches 160 codes. These values describe detail; they do not classify it as expendable.

A row-independent per-column reference underfits the vertical variation. A global ten-coefficient cubic surface also underfits severely. A deliberately richer but fixed 72-coefficient tensor spline uses 64x32 tiles, training-tile Y lower quartiles and quantile regression, with each tile evaluated by the fold that did not train on it. This is model development on the known ad, with spatial validation inside that same frame, not independent video validation. The model preserves original pixels and large residual peaks. It passes the spatial summary tolerances while failing the stricter constant-matte ±4-code requirements. The Python fit/full-band scans are not a production performance result.

At the actual 6x48 guard-band sample positions, the spline residual has black-compatible fraction .98958, signed P90 12.90, spread13.47 and continuity1.0; original texture 6.95 and neutral fraction 1.0 are unchanged. These are separate from nominal-edge contrast. Measuring contrast at guard 293/row 295 yields 1.736 because it is inside the retained fringe; that is a geometry misuse warning, not weak evidence at the true 300/301 transition.

The denser spline-relative visibility test initially flags 27 probes and two adjacent occupied rows. Mapping these coordinates reveals 22 model-underprediction flags at raw Y 80..96, below or equal to the original raw visibility cutoff 96; they have no local17 residual. Only five probes are both raw-visible and spline-relative-visible, and those correspond to localized residual detail on separate sampled rows. The model must not invent new foreground merely by predicting a reference below the observed black floor.

A subsequent explicit design revision therefore evaluates raw witnesses against the qualified reference: for luma, `Y > max(original visibility cutoff, predicted reference +32)`, preserving the original chroma witness separately. On this ad the intersection has five probes and no occupied adjacent-row pair. That removes an artifact of the fit, but it does not prove the five remaining details harmless. The sparse-grid controls demonstrate why this result alone cannot authorize cropping.

See [native structure](profile/native-findings.md), [fixed spline and revisions](profile/spline-surface-findings.md), [probe coordinates](profile/spline-visible-probe-coordinates.json), [raw/residual intersection](profile/spline-visible-probe-reconciliation.json), and [production predicate trace](integration/predicate-and-route-findings.md).

## Subtitle and regression evidence

Three additional actual captures were decoded with unchanged beta `AnalysisLumaSource::Sample` and independently unpacked; every Y/U/V code matched at all 8,294,400 pixel coordinates per capture. Missing range fields remain Unknown. City-lights is trusted at 66..2092 and retention-safe. Thai and Latin pictures are trusted at 274..1884, but raw retention preserves caption/content outside those bounds: Thai top 251 and Latin bottom 1933. Those envelopes must not redefine the picture aspect.

The nominal Thai grid detects adjacent caption support, while moving the sampling phase to an eight-row guarded exterior loses that adjacency. Latin local-reference processing weakens the raw adjacency even at nominal geometry. Thus qualification must retain both the original nominal-boundary visibility/continuation scan and guarded exterior checks; the guard cannot replace the original sampling geometry. The frozen morphology veto catches these two real captions, but misses engineered smaller/fainter ones.

Ten full-resolution 4K P210 controls were generated independently of the ad-calibrated morphology rule, with Python serialization roundtrip and every-pixel beta sampler checks. Strong edge nomination succeeds on all; this is deliberately not enough to establish crop safety. The relaxed morphology gate passes gradient/grain positives plus faint/subpixel glyphs, 24 single-pixel stars, a 72-pixel logo and a broad +8-code object; it rejects a structured ceiling/panel and a large near-boundary subtitle. The fixed spline model's aggregate fit also passes the small-content cases, while keeping their residual peaks in the measurements. Sparse median-relative/intersection visibility misses the small-content cases. Spline model-fit and median-relative visibility were separate component experiments; no end-to-end spline crop route was tested.

Unchanged beta keeps all ten controls PROVISIONAL and refuses their supplied inset. Promoting the newly admitted negative cases would therefore risk newly introduced false crops. This differs from the earlier smaller +8 control that beta already trusted: the new raised-gradient context makes the incremental risk concrete. No production regression was introduced because none of these rules was enabled. These engineered controls describe failure modes, not their prevalence in real programs.

The [regression report](regression/final-research-report.md) contains the case table, verified range/threshold information and corrected nominal/guarded comparisons. Keep global-near-black cutoff separate from per-edge visible cutoff, which is bar threshold+8. Also preserve the distinction between a byte-identical semantic alias and a distinguishable real negative.

## Concrete design to pursue

1. **Independent boundary discovery.** Add a bounded relative-edge nominee alongside the existing detector. Use current native Y/chroma change, straight-line support across independent image regions and persistent inward picture support. Each edge must be measured independently. Report nominal boundary, uncertainty and identity explicitly.
2. **Per-edge background evidence.** Estimate a constrained spatial reference from separate samples, validate it on excluded samples, and retain original source measurements. Top and bottom may have different levels/profiles. Reject unstable/unsupported models; do not force a flat background, share a global floor, change range interpretation or fabricate normalized source pixels.
3. **Separate protected foreground.** Preserve localized structure and weak stroke groups independently of aggregate background fit. The current area/box relaxation and sparse relative grid are disproven as sufficient safeguards. The next targeted experiment should use full-resolution/local refinement around residual structures, weak-text grouping and known current caption evidence, with the recorded tiny-glyph/star/logo cases as mandatory negatives. It must not train the reference on a caption mask or silently drop an unexplained raw witness.
4. **Separate geometry from preservation.** Nominal picture bounds supply the measured aspect; conservative fringe and current caption/foreground envelope supply preservation constraints. Existing subtitle ownership and translation can manage confirmed captions. They must not authorize arbitrary non-subtitle detail. Unresolved foreground requires a conservative retained extent, not an invented target aspect or ignored pixels.
5. **Publish only a complete route.** Bind both edge/reference results to exact source identity, format/range, generation, current owner and base geometry; retain current-pixel final admission and existing independent proof continuity. Strong complete evidence can use the existing short confirmation route. More time or pooled partial evidence cannot make uncertain exterior content safe. Keep the working strict/dark routes when the new cue abstains, and add no queue wait or growth.

The first production-facing step should be diagnostic emission of these separate fields and reasons, with paired replay against the actual ad, five older native captures, and the new negative controls. A crop-enabled version needs a foreground safeguard that passes those comparisons, caption pixel preservation, moving-transition tests and an analysis-budget measurement. This research gives a concrete implementation direction and regression fixtures; it does not justify turning the current prototypes into crop authority.

As external background, FFmpeg also separates absolute-black crop detection from motion/edge detection. Its motion/edge implementation requires motion-vector side data, so it is not a drop-in answer for this paused native HDMI capture. This supports considering independent edge evidence, not any particular VP safety policy. [FFmpeg primary source](https://www.ffmpeg.org/doxygen/trunk/vf__cropdetect_8c_source.html).

## Audit and archive

The first continuation probe used 48 columns; the corrected probe uses production 256 and still confirms raw continuation. The earlier 48 output is labeled exploratory. The real-capture comparison initially confused global-darkness cutoff with per-edge visibility cutoff for City-lights; the corrected result uses actual bar threshold+8. Thai/Latin and Limited synthetic controls happen to use 96 for both but the concepts remain separate. The prior research pass's invalid row-interleaved P210 control run remains separately quarantined; these new P210 controls have full-plane/every-pixel verification.

Archived text, numeric outputs, scripts, logs and hashes are review artifacts. Native and derived pixel buffers, previews, executables, objects and probe-mask binary arrays remain local. Protocols state where constants were fixed before evaluation and where later design revisions occurred. The source worktree and deployed binaries remain unchanged; VP-0218 stays In Progress.


Large numeric JSON files in this archive use compact serialization with parsed-value equality checked against the local originals. The archive manifest records both original-source and archived SHA256 values where bytes differ; no numeric records were removed. Other diagnostic artifacts retain their original bytes.
