# Per-edge local-reference fit measurement

Frozen method in [local-reference-protocol.txt](local-reference-protocol.txt), SHA-256 `85ABC26B1C07372098A1851AE6974D3CB2F3D9952C070A7790269A84A1552E93`. For each x, the profile is median Y across y=0..292, smoothed by a 129-column median. No pixel values were changed and raw witnesses are reported separately.

Across all four unique payloads the estimated profile spans Y=68..196. The signed source-minus-profile residual has P10/P50/P90/P95/P99/max = -4/0/4/8/12/156–160. Only 90.8% of pixels are within ±4; p90-p10=8. Thus this simple per-column profile fails the unchanged ±4 exact sample gate and p90-p10≤4. Neutral chroma passes at 100%, but about 49.0% of raw top-band pixels exceed Limited floor+24 (Y=88). Those high values are retained evidence, not normalized away.

This establishes that a one-dimensional column profile cannot model the upper band's row-dependent gradient. The exact 17x17 2-D local median gives much smaller residual percentiles (P95=0, P99=4), showing that a more local reference can fit the smooth field. It also leaves fixed spatial residual components with peaks up to160, so a model-fit pass alone does not establish that the excluded pixels are expendable. Acquisition geometry, background-model fit, and raw content veto remain separate questions.

The four samples are consecutive frames from one clip, not independent scenes. See [local-reference-results.json](local-reference-results.json) and [local_reference_measurement.py](local_reference_measurement.py). No source changes were made.
