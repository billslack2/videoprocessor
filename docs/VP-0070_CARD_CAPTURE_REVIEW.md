# Opaque subtitle card capture review — 2026-10-08

## Reproduced failures

The 11-33-11 recording is a 2560x1440 desktop. Its video viewport is
[544,176,2544,1301], or 2000x1125. Source-scale replay therefore crops this
viewport before re-expanding it to 3840x2160. These proxies cannot restore native
HDMI samples, but avoid treating window chrome/desktop as video.

* 18–22 s: the professor question mark is clipped vertically at its right edge;
  unprocessed replay at 90–94 s is intact. Identical proxy pixels produce glyph
  right edges 2384 and 2396 when the supplied picture edge changes by one row.
  Both measurements already find a sufficiently wide black card.
* 34–38 s: a strip below the original card is copied upward inside the overlay.
  This is distinct from final overlay attachment to the physical black bar.
* 43.4–43.9 s: only “ach it?” moves from a two-line caption. The failure predates
  pause controls. Accepted geometry covers only the tail, despite finite opaque
  backing around the complete cue.

## Conflicting responsibilities found

Component IDs were derived from vector size even after detached-dot merging
removed entries. Later discovery reused a live ID, mixing ownership of unrelated
components. A raster regression reproduced loss of 72 of 864 accepted glyph pixels.

Frozen overall cue bounds clipped fresh extraction lines; conversely confirmed
extensions could enlarge overall bounds without enlarging extraction lines.
Display stability and current pixel capture were therefore using conflicting
rectangles. Cleanup fringes also supplied picture-side capture authority, while
small disagreement between subtitle and physical picture edges let picture rows
bypass card checks as if they belonged to the black bar.

## Corrected responsibilities

1. Conservative text evidence nominates a cue. Component IDs remain unique for
   the entire analysis pass.
2. Current opaque-card proof authorizes recovery of glyph content. Native edge
   refinement is bounded and stays inside measured backing; display padding and
   cleanup fringes do not authorize glyphs.
3. Verified current extraction lines are independent of frozen display geometry.
   Confirmed extensions reach extraction. The displayed envelope grows only as
   needed to contain verified content and never shrinks within the same cue.
4. Cleanup follows measured original backing. Small physical-edge reconciliation
   belongs to composition only and does not change aspect-ratio decisions.
5. Uncertain boundary rows require current opaque backing proof, so rejecting a
   picture strip does not also remove genuine glyphs crossing that edge.

Wide-card completion is implemented. Near-bar nomination now remains separate
from physical bar-ink evidence: picture shapes near the opposite bar cannot set
the caption font scale. Native fringe growth assigns every recovered component to
one existing text row, and whole-card recovery distinguishes learned foreground
cores from weaker antialias coverage used only to verify backing.

The three source-scale GPU replay outputs now retain the question mark, move both
lines of the partial cue, and omit the copied strip. HDR/PQ replay is a rendering
simulation from the recorded image, not recovery of original HDR samples. Current
native tests cover the same ownership and boundary behavior on controlled pixels.

Earlier successful static-frame checks did not exercise temporal freezing or the
correct recording viewport, and were not evidence that these live failures were
resolved. Final regression qualification is recorded with the build logs. Live
HDMI playback remains a separate user validation step.

## Qualified build

The final clean x64 Release rebuild passed 2,283/2,283 native tests and
92/92 configuration checks. The focused subtitle run passed 284/284 tests.
Three recording-derived GPU replays were rerun against that exact build and
visually inspected: the complete question mark, both lines of the partial cue,
and the reconstructed card without the copied bright strip.

The configuration suite reported that physical two-monitor placement could not
run on this one-monitor machine; synthetic placement coverage passed. No native
HDMI performance or live playback result is claimed from these recording proxies.
The shader gains no extra GPU pass or readback. Detection remains on its existing
worker, with bounded native refinement and reusable scratch storage.

`architecture-verification.json` pins the source changes, Release binaries,
test logs, and replay inputs/outputs. This build has not been deployed.


## Glyph-row and backing-seam follow-up

Boundary ownership now corroborates faint antialiased pixels against nearby bright glyph cores. Native glyph completion uses the same connection within three uncertain boundary rows, charged to the existing sample budget. This prevents a backing boundary from erasing a row inside letters while retaining scene-stripe rejection and joined-script support.

Attached cleanup follows physical picture-edge reconciliation by up to three pixels before backdrop filtering. It does not expand glyph-copy authority, bridge a finite card gap, or alter real viewport cropping. Solid and reconstructed GPU tests cover both edges; the glyph sample matches the independent oracle (119), and cleanup tests cover blur 0, 3, and 30. Four recording-derived replays were visually inspected for the question mark, partial cue, lower seam, and atomic-bomb cue.

Smart/percentage HDR analysis controls now live in Subtitles and apply to all subtitle styles. Existing Zoom values remain a read-only compatibility fallback until overridden; controls are removed from Zoom. Smart moved-subtitle coverage is measured from the trusted analysis picture edges. The inline picker retains only basic colors, hue/saturation and brightness, plus the existing external hex field.

The follow-up qualification receipt is `seam-config-verification.json`. It records final tests, artifacts and replay hashes separately from the earlier architecture build. The first full native run had one existing configuration-cache timing failure (same-size immediate rewrite with restored write time); the isolated rerun passed. Preserve that result in the receipt rather than treating the initial run as clean. Recording proxies do not establish native HDMI performance or replace live playback validation.

Both final full native attempts returned 2,291/2,292 with the same pre-existing configuration-cache test failure; the full suite is not clean. All subtitle/GPU tests and four recording replays passed. No deployment was performed for this follow-up.


## Compositor ownership gap (follow-up after live deployment)

The prior detector antialias fix did not close all live cases. At 13:44:39 the live log records card bottom 1897, physical picture bottom 1898 and glyph line 1868..1968: the glyph is fully captured, but the shader rejects row1897 as picture outside the measured card. This cuts a line inside letters independently of blur, padding or OCR.

`CompositorBoundaryGapCannotEraseInteriorGlyphRows` reproduces the missing row with explicit complete glyph geometry and no detector; it failed against the deployed shader. The shader now admits connected bright strokes only in the bounded (1..3 row) disagreement between card and picture edge, corroborated against existing glyphs immediately outside that strip. It retains per-line bounds, rejects detached bright content, and adds no pass/readback. Tests cover both edges, solid/reconstructed, through strokes, terminal strokes, and detached content. No detector, extraction geometry, display padding, configuration or blur change is part of this correction.

See `glyph-gap-verification.json` for the exact Release build and focused regression results. The prior full-suite configuration-cache failure remains recorded separately; no clean full-suite claim is made for this follow-up.


## Subtitle acquisition and continuity review — 2026-10-08

The supported input is bright glyphs on opaque black backing. Backing may touch/cross the letterbox, be entirely inside it, or be close to it inside the picture. Black scenery can hide the card boundary; connected darkness alone is not a measurable card edge.

### Evidence

The 14:13:37–14:36:09 log snapshot contains nine suspected flashes preceded by `analysis-work-limit`, followed by the same rectangles after 41.7–125.1 ms. The existing grace period was bypassed. AR/bar authority stayed valid. Other losses cannot all be labeled misses because the available raw recordings predate those logs.

Controlled reproducers also showed unrelated bright flecks changing the foreground palette, unbacked texture exhausting the component budget, inconsistent near-bar distance checks, pending future observations vetoing a current onset, and cached old geometry delaying discovery of additional text.

### Corrections

- Calibrate foreground from a credible near-bar glyph row with continuous opaque surrounding support. Prefer the nearest credible row, so unrelated brighter text does not erase the caption.
- Reject clearly unbacked picture components before they exhaust the existing component-label budget. Retain bounded work limits and independent raw evidence.
- Separate backing proximity from cleanup/display padding. Refine visible edges at source-pixel resolution. Infer a hidden edge only with bounded support, tightened by the measured opposite inset when available. Completely hidden boundaries retain a one-glyph-height bound; no flood fill through black scenery.
- Pass verified near-bar eligibility through temporal confirmation and placement. Placement cannot enlarge extraction/cleanup merely to qualify.
- Scan fresh geometry on every analyzed source frame. Remove the old fingerprint shortcut that could stamp previous geometry and ink onto a new frame. Safe same-frame sample/buffer reuse remains available under existing configuration.
- Treat pending/stale future measurements as unknown; they neither confirm nor veto current evidence.
- Allow a resource-limit miss to use the configured hold only with complete, fresh, matching current glyph/backing evidence, including learned extensions. Changed text, missing glyphs, incomplete evidence and context discontinuities still release. Repeated presentation cannot replenish the hold.
- Use verified opaque card interiors for live crowded-row confirmation instead of legacy translucent-edge hints.
- Log raw rejection reason, component count, work-limit state, and measured/inferred proximity separately from accepted output. Rejection changes can appear even when no cue ever becomes visible; diagnostic emission is throttled.

### Scope and limits

AR authority and the deployed glyph-boundary shader are unchanged. No GPU pass, readback, render-thread wait, or reduced sampling cadence is introduced. Removing cached geometry increases worker CPU demand; it is an intentional correctness tradeoff. Existing logs showed zero dropped/failed worker jobs, but live performance after the changes must be checked separately.

Independent static detector replay used decoded recording viewports resized to 4K, with mirrored and interpolation variants. These are not native HDMI samples, continuous onset tests, GPU timing or a broad false-positive-rate measurement. One artificial top-mirrored partial cue contains a dark resampling fringe; the reviewer preserved that caveat rather than claiming it is proven harmless in native top-caption footage.

Hidden-edge proximity is an inference with a deliberate recall limit: if one measured margin is 5 pixels and the hidden margin is actually 9, a true 20-pixel gap can be inferred as 24 and rejected. The same asymmetric card at gaps 10/15 is covered by positive tests. Completely hidden backing has no exact observable boundary; increasing this tolerance further without source evidence would weaken the distant-shadow rejection.

The qualification receipt records final build, test, replay and review evidence. No deployment is part of this review request.

### Final qualification

The clean x64 Release rebuild and final incremental build succeeded. The initial incremental attempt failed with corrupt linker debugging information; the clean rebuild resolved that build artifact issue.

The first focused run passed 338/340. The distant-shadow failure led to the opposite-inset production correction and mirrored near/far plus asymmetric-margin regressions. The resource-limit failure was an old unconditional-release assertion; it now requires current-pixel grace and pairs that with incomplete-evidence rejection. Final focused tests pass 341/341; the final full native suite passes 2,309/2,309, including AR and GPU coverage. The historical configuration-cache failure did not recur in this run.

Five GPU replays of the existing question-mark, two-line sliver, partial-caption, atomic-bomb and Einstein screenshot cases passed and were visually inspected. Complete glyphs remain visible without the former internal line or copied-sliver artifact in these samples. All 32 positive and 16 negative independent detector replay variants retain expected behavior, without work-limit failure. The final cap produces exactly the same detection and geometry outputs as the prior candidate for those 48 variants.

The earlier paired detector benchmark changed mean-of-case-medians from 12.294 to 12.466 ms (+1.4%); caption-free inputs added roughly 0.8–1.5 ms. That benchmark preceded the final small inset cap. Final-source behavioral replay overlapped native CPU benchmarks and is not a controlled performance comparison. Removing old-geometry refresh adds further CPU demand; the prior live logs suggest roughly 0.92 ms/source-frame savings were being traded for stale geometry, an estimate rather than a measured new-build A/B result. Live worker telemetry must validate the final cost.

`subtitle-misses-verification.json` pins the final source, Release artifacts, logs, independent review and replay inputs/outputs. The installed build and user configuration were not changed.
