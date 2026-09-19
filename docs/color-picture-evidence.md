# Experimental color evidence for dark picture detection

This is diagnostic-only. It does not change cropping, crop retention, near-black handling, NLS eligibility, or source geometry.

## Enable for a local investigation

Launch VideoProcessor with the process environment variable `VP_COLOR_PICTURE_EVIDENCE=shadow`. Unset is off; all other values, including `retain`, are rejected. No persistent configuration setting is installed or changed.

The renderer samples at most once every two seconds, for at most 300 snapshots per renderer instance. Recreating the renderer starts a new budget. Each snapshot takes at most 4,608 source-coordinate samples and writes one summary plus four edge records. No full-frame conversion or additional GPU readback is introduced. Sampling also works when ordinary crop analysis is waiting.

`Alpha color-picture evidence` reports source generation, sequence, presentation epoch, input format/encoding, precision support, whether the weak full-frame hypothesis matches, and whether previously committed full-raster geometry exists. The summary identifies `mode=shadow` and `policy_effect=none`. Edge records compare robust Y/U/V medians and spreads across four sectors of shallow outer strips and paired interior strips.


Every black-level and color-picture diagnostic record includes an opaque `instance` identifier created once per renderer instance. It remains stable across source-generation changes and changes when the renderer is recreated, including after process restart or module reload. Summary, row, completion and edge records carry the same identifier; color edge records also carry `generation`. Keep the diagnostic families separate and group samples by `(family, instance, generation, sample)`, never by `(generation, sample)` alone. Instance fields are additive to schema 1. Older logs without an instance field require explicit session segmentation; do not silently overwrite repeated legacy keys.

## Interpretation and limitation

The hypothesis uses color, distributed detail and edge/interior agreement together. Uniform tint, a central logo, insufficient precision, and a sufficiently clear bar/interior difference abstain. A positive result is NOT a confidence percentage or permission to crop, retain full frame, or stretch.

The independent review found a concrete ambiguity: subtly tinted, noisy bars can have the same statistics as a dark full-frame star field. A regression test deliberately preserves that counterexample. Neither repeated measurements nor previous full-frame authority alone resolves it across gradual format transitions. The result therefore remains observational until a stronger discriminator is validated.

For the September 18 paused shot, raw HDMI samples supported star-field detail reaching the 3840x2160 source edges; Photoshop RGB-zero surrounding space was not observed as embedded source bars. This does not prove that weak color evidence can safely classify all similar-looking frames.

## Formats

The shared AnalysisLumaSource sampler supplies Y/U/V for P010, P210, native v210 and supported native RGB. P010 chroma is vertically subsampled, so tiny differences may not survive conversion. This experiment abstains for known 8-bit or unknown source precision. Ten-bit and twelve-bit RGB samples use the existing analysis conversion; no hue-angle calculation is used.

## Acceptance before any active policy

Demonstrate separation from noisy/tinted bars, gradients, logos, credits, subtitles and sparse stars; validate both directions of real aspect changes, startup versus retention, generation resets and stale/repeated frames. Match decisions across native and converted representations where information is equivalent, and abstain where precision is lost. Benchmark bounded sampling in Release. Only then propose a separately reviewed geometry-policy change.
