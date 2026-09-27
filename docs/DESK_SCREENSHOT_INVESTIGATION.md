# German TV desk screenshot investigation — 2026-09-27

The larger browser screenshot confirms a displayed approximately16:9 video inside a wider browser viewport. It includes a channel logo, player controls, and browser framing; it is not the original4K AppleTV capture. Do not pass those overlays off as controls-hidden source pixels or map its screenshot coordinates to logged crop bounds as exact measurements.

## Findings

1. Native extraction has a demonstrated false-bar ambiguity. `ScanBlackLine` accepts44/48 dark samples; `InspectHorizontalEdge` accepts95% black samples with low p90/texture and approximately symmetric opposing bars. A small object can be sampled yet diluted by these aggregate gates, or missed between columns. This is an intentional tolerance for overlays/noise, but also admits dark authored scenery. `SparseNativeBarsCanContradictVisibleExtentSafety` explicitly reproduces that weakness with real synthetic4K P010/P210 pixels, not reconstructed television pixels.
2. Existing dense visible-extent inspection can contradict that sparse acquisition. The same regression verifies occupied candidate bands are marked unsafe. Final new-crop admission refuses a resolved rectangle that excludes its fresh visible witness; a larger FIT preserving the witness may still acquire. This does not prove the original black regions are cinematic bars.
3. The supplied log already records unsafe bands and expanded FIT presentation. In the first likely burst, logical2.12860:1 acquired at0,176–3840,1980; the FIT later became0,94–3840,2052. The old optional fill used the1.96118:1 envelope against the2.0 limit and removed180pixels per side. Another burst removed190/192pixels. The earlier deployed content-reference correction vetoes those exact fill operations because the underlying logical aspect exceeds the configured limit.
4. Bright sides inside the retained aperture cannot universally veto a top/bottom crop: genuine letterboxed pictures routinely have exactly that geometry. Side-region picture continuing into a band proposed for removal is contradictory evidence and must participate in the discarded-region check. A bright station logo in a true bar is a separate ambiguity; tightening to zero nonblack pixels would break supported caption/logo behavior.
5. The current relative-contrast fallback is outward-only and requires two independently qualified common native aspect families. It does not authorize this initial desk crop or learn the logged~2.10/~2.13 rectangles as common aspect families.

## Validation

Re-ran14 focused existing tests using the exact Release test binary matching the deployed source: all14passed. Coverage includes the sparse/dense contradiction, real symmetric bars with picture across the width, lone/disconnected desk edges, ZDF new-crop acquisition/visible-witness controls, and both logged optional-fill bursts. Results in `artifacts/desk-review/tests.trx`.

No new production rule, threshold change, deployment or configuration edit was made. Existing test expectations were not weakened. No additional test was added merely to duplicate the already explicit sparse/dense false-bar characterization.

## Decision

The screenshot is valuable scene context, but is not100% evidence that side brightness alone identifies full-raster content. The safe target remains fresh contradictory picture detail inside the proposed discarded regions, followed through to final fill. Current evidence confirms the initial detector weakness and the historical downstream fill bug, not a new demonstrated bypass of the current visible-pixel guard. Do not add a blanket side-content veto or claim the complete scene is fixed.

For further correction, compare an original controls-hidden frame or short sequence from this scene against native candidate geometry, dense visible bounds, and final output. It can distinguish genuinely missed localized picture from intentional black scenery indistinguishable from bars. The visible browser URL provides a potential route to reproduce the scene, but this investigation did not assume the browser feed and AppleTV feed are pixel-identical or access/play the site.


## Authorized correction: connected picture at the candidate boundary

The subsequent implementation request authorized a narrow fix to the demonstrated acquisition weakness. `DeskConnectedSidePictureCannotAcquireAFalseVerticalCrop` runs real synthetic source pixels through extraction, symmetric recovery, and 48 transition-model observations. Before the production change it failed because connected picture occupying a proposed bar became logical crop authority. Three companion controls passed. The red result is preserved in `artifacts/validation/red-connected-desk/tests.trx`.

The correction checks neighboring dense sample columns at three rows spanning each otherwise trusted top/bottom boundary. All three rows must contain credible picture in both columns. A whole native scan step separates the outside probe from the edge to tolerate filtering fringe. It reuses existing luma/chroma visibility thresholds, preserves raw measurements, and marks the contradicted edge untrusted with `bar-picture-continuation`. This is provisional evidence, not full-raster authority; existing retention and presentation logic still govern an established crop. Symmetric recovery resamples its actual inferred boundary with the same check.

The previous `SparseNativeBarsCanContradictVisibleExtentSafety` deliberately asserted the old erroneous acceptance. Its authority assertion now requires rejection; its sparse aggregate statistics and dense excluded-band safety assertions remain, evaluated against the proposed rectangle. This changes the documented bug characterization into a prevention regression without weakening the visibility checks.

The check applies to picture anywhere across the proposed discarded horizontal bands, including their sides. Bright side content inside the retained picture remains compatible with genuine scope. It does not attempt to distinguish perfectly black scenery from a pixel-identical encoded bar, and does not establish that the exact unreproduced German TV sequence is fixed.

Independent design review approved the shared edge qualification approach and checked native promotion, symmetric recovery, learned geometry admission, and relative-contrast fallback for bypasses. No deployment or configuration change is part of this correction request.


### Review follow-through

The first clean full-suite run passed 1830/1831 tests. The sole failure was the precondition in `QuantizedShadowedSideRejectsDeeperPictureCaptionsAndRamps`: pattern 2 draws a bright object continuously from row 267 through the picture boundary at 276. Previously it remained a trusted native edge and a later strict profile blocked it. The revised test explicitly requires provisional evidence and the new contradiction reason for that pattern, keeps its measured geometry checks and blocking assertions, and leaves all other patterns' original expectations intact.

Dense columns are capped by source width so a small raster cannot count one physical pixel twice. Tests also exercise a caption-expanded raw boundary whose mirrored candidate crosses a narrow attached object; recovery must reject that actual inferred boundary while the disconnected-caption control still succeeds.

The new native pass reads at most 256 columns per candidate edge and three rows per column (1536 samples for two edges). Clean black bars short-circuit after the first outside sample at each column (512 samples total). No image downscale, full-frame copy, temporal waiting, or lookahead policy change is introduced by this correction.


### Final validation

Clean x64 Release rebuild succeeded. All **1833/1833** native tests passed, including seven new desk-veto methods; results are preserved in `artifacts/validation/final-desk-veto/tests.trx` and build/test logs alongside it. Independent final review found no blocking concern. `git diff --check` passed. Correction-only diff against the previously deployed source is `artifacts/desk-review/desk-correction.diff`.

The current desktop deployment and user configuration are unchanged. The corrected build is ready for a separately requested deployment and real-content validation. The exact German TV source was not replayed.
