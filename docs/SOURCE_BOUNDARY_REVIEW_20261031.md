# Source-boundary review, 2026-10-31

Five independent Luna xhigh research passes reviewed spatial detection, lookahead,
known/remembered ratios, adversarial scenes, and integration with the existing
source-evidence pipeline. This document records the design decision, not a claim
that a new runtime has passed full qualification or manual playback.

## Decision

Evolve the existing source-pixel measurement and immutable source-evidence packet.
Use spatial shape and contrast relative to a locally measured matte. A small
amount of noise, chroma variation, a raised black floor, or localized subtitle/UI
pixels must not invalidate an otherwise well-supported border. Apply the same
principle to SDR, HDR and decoded Dolby Vision; no DV-only exemption is warranted.

The working observer fits each column's reference from three source rows and
checks two additional rows. It masks contaminated columns and reuses the mask
through coarse nomination, refinement and inward continuity checks. Acceptance
requires broad separated support. A raster-sized nomination remains a search
hint: it must not prevent finding encoded bars inside the input raster.

A brighter interior gradient is not the picture boundary. The first sustained
source departure must be preserved; a faint coherent collar or competing border
can require uncertainty. Short detached bar graphics must not become boundaries.
The measured physical rectangle and conservative pixel-preservation envelope
remain distinct. An observer estimate does not independently authorize a crop.
The optional physical observer retains its existing two-millisecond budget.

## Dynamic transitions and useful priors

Already available lookahead can corroborate a change without another scan timer
or fixed real-time wait. Existing queued depths are three frames through 30 Hz
and five above 30 Hz; these are capacity limits, not a three-of-five vote or a
mandatory delay. Current exact source identities, generation and continuity must
match. Rewind, seek and source replacement invalidate incompatible evidence.
The 24 distinct displayed frames in renderer tests are an observation window,
never a runtime transition requirement.

Known ratios and remembered exact rectangles should rank candidate boundaries
and focus pixel work. They do not replace source evidence or round a measurement
to a screen/standard ratio. A current visible edge can anchor a candidate for an
occluded opposite edge; current pixel checks must still validate its proposed
extent and reject contradictions. Broad real picture outside the old crop can
withdraw old authority promptly even before every new boundary is exact.

Existing remembered-family code retains exact measured representatives, learns
from qualified native geometry across scenes, and checks current pixels on recall.
Its three-family capacity and common-ratio learning eligibility are limitations
identified by the review. Expanding those is a follow-up experiment with explicit
nonstandard-ratio, stale-history and competing-edge tests, not part of this
observer patch. Caption geometry must never train picture history.

Above 30 Hz, an unavailable analysis input may retain one previously submitted
whole image, with the original displayed identity, geometry, caption and expiry.
It cannot manufacture new measurement votes or renew a cue. Shader warmup is a
separate startup state: the operator permits caption clipping while compilation
is pending. Any warmup-image reuse must preserve that original Pending provenance
and must not weaken post-composition checks.

## External comparisons

[FFmpeg cropdetect](https://ffmpeg.org/doxygen/8.0/vf__cropdetect_8c_source.html)
provides scanline aggregation, outlier handling and an alternative edge/motion
mode. These are useful comparison points; HDMI captures do not provide decoder
motion-vector side data. Its output rounding or temporal defaults are not VP
requirements.

[Hyperion's detector](https://raw.githubusercontent.com/hyperion-project/hyperion.ng/master/include/blackborder/BlackBorderDetector.h)
uses multiple scanlines and includes an OSD-oriented mode. Distributed sampling
is relevant, but a lighting-oriented symmetric border result cannot substitute
for VP's independently measured source coordinates and picture-preservation proof.

Our inference from these comparisons and the existing VP architecture is that a
bounded spatial estimator with robust reference strips, explicit occlusion masks,
source-bound lookahead and verified candidate priors is a better fit than importing
a generic crop filter or adding a long temporal majority gate.

## Required evidence

Keep failing-first tests for noisy/raised/tinted matte across native formats and
ranges; localized captions/UI; raster-hint bar acquisition; faint picture collars;
short detached graphics; asymmetric, pillarbox and windowbox geometry; exact
nonstandard ratios; source resets; and bounded cancellation. Replay supplied
native captures separately from synthetic controls and report actual frame counts.

Actual renderer checks must cover both shipped dependencies, 23.976/24 and
59.94/60 input cadence where applicable, source ownership, original whole-image
repeat and fresh successor, native-boundary independence from captions, and
transition output geometry. Warm-cache iteration does not replace cold shader
coverage. Partial evidence cannot issue a full runtime qualification receipt.

## Native validation checkpoint, 2026-11-01

All 837 mandatory native methods pass in run 20261031-235938-d9be7b97,
including all 64 earlier native captured frames. That set measures source rows
263..1897 at 3840x2160 (2.3500611995:1), retaining the distinct 254..1906 guard.
All 56 newer paused-capture frames also pass in 20261101-000123-67b138bc:
275..1884 (2.3865755127:1), retaining their recorded 274..1884 guard. No standard
ratio, screen aspect or symmetric rounding supplied either measurement.

Observer-only replay cost, excluding file I/O, averaged 0.3431 ms (peak 0.5358)
for the 64-frame set and 0.3096 ms (peak 0.5288) for the 56-frame set. These are
controlled replay measurements, not a guarantee of live scheduling latency.
The optional two-millisecond cancellation budget remains unchanged.

The supplied 95-frame V210 full-raster set has a separate dim-edge uncertainty.
Its left-edge profile is spatially structured but does not meet the current
terminal brightness margin. A texture/coherence fallback needs adversarial
raised and asymmetric matte tests before implementation; this patch does not
claim that case resolved. The source capture also postdates the screenshot and
must not be used as pixel proof of that earlier instant.

Actual renderer qualification, cold shader preparation and dual-dependency GPU
checks remain separate from this native checkpoint. No new live playback or
full qualification is claimed by these results.
