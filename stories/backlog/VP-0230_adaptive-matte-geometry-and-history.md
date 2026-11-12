# VP-0230: Detect active picture using adaptive matte contrast, geometry and recent history

## Status

Backlog

Requested 2026-11-11. Continue from the VP-0228 clean Eternals investigation. First implement an offline comparison of measured matte contrast, opposing boundary geometry and recent verified history against the existing detector. Record a candidate's actual decision and presentation behavior before promoting it to live use.

## User story

As a viewer, I want VP to recognize the actual picture boundaries promptly when dark picture replaces encoded bars, using the relationship between the surrounding matte and picture, including the shape and location of opposing bands and recent verified observations. Detection must remain accurate across varying black levels, dark scenes, captions and overlays.

## Problem and existing evidence

The detector already estimates a frame-local black reference and measures opposing edges. Later broad-picture admission uses absolute 10-bit luma/texture alternatives of 112 and 12, and the relative-contrast path retains absolute occupancy and strong-strip thresholds of 104 and 112. Review how these decisions interact with declared range, measured matte level and noise.

In the menu-free Eternals transition, native bounds change at source 4192 from scope to 0,66–3840,2092. Live presentation retains scope until 4196. The lower exposed strip has luma P90 around 101–103 and texture around 9–11. The exterior matte has median luma zero and chroma departure from neutral around 1–2 codes, while the lower picture strip has median chroma departure around 26. The stored 12-bit values remain below the equivalent absolute luma threshold. These observations support testing relative evidence and do not establish a decoding or precision fault.

An eight-row outward inspection envelope passes the relative matte check while preserving the exact native measurement separately. Actual live adoption also encounters subtitle drift, a latched presentation owner, unavailable lookahead measurements and independent history requirements.

Existing counterexamples defeat depth, width, seam continuity, texture, simple temporal co-change and color used individually. A soft textured panel with real picture visible at its sides passes the experimental three-frame split proof under hypothetical qualified history and a clear owner. A tinted variant also passes that proof. Preserve these controls and test remembered geometry against panels deliberately matching previously observed bounds.

## Proposed investigation

1. Audit the evidence path from source format/range and Dolby decoding through matte estimation, edge geometry, picture qualification and final admission. Identify where fixed code values override a well-supported measured reference. Use the existing Y and chroma samples to test color contrast. Compare RGB or additional precision only where it supplies independently useful evidence, with matched source and conversion provenance.
2. Estimate each exterior band's luma, chroma, spatial variation and noise from distributed current samples. Compare adjacent picture to that reference with tolerances informed by measured variation and source quantization. Test raised black, gradients, fades and low-contrast picture. Record uncertainty and the samples responsible for rejection.
3. Combine opposing edge evidence with shape and location. Check coverage across the width, boundary consistency, band uniformity and contrast inside the proposed picture. Similar opposing heights provide corroboration with tolerance for asymmetric framing, captions and source artifacts. Derive bounds from source pixels and allow arbitrary measured aspects.
4. Evaluate bounded memory of independently verified matte statistics and native boundary positions. Track confidence, age and context. Refresh with current pixels, weaken stale evidence and invalidate it on incompatible source/format/domain/profile changes. Avoid training history from the candidate it is meant to corroborate or letting overlays become self-confirming history. Test first occurrence as well as returns to known geometry.
5. Replay the combined evidence through real admission and presentation entry points, including active subtitle drift, pending/unknown caption work and current caption envelopes. Keep native geometry and retained presentation margins explicit. Account for actual readiness of buffered frames and test fallback behavior when optional evidence is unavailable.
6. Iterate on failures with measured explanations, then implement the smallest justified change and validate its integration and real-time cost.

## Continuation material

- Clean capture directory is C:\Videoprocessor\vp\logs\analysis-20261111-162516-32736-2. Frames 000003–000007 contain sources 4191–4195, with exact identities in sidecars and frames.jsonl. analysis.jsonl records owner, readiness and presentation decisions. The trusted scope base is 0,274–3840,1884, distinct from the preceding raw measurement 0,266–3840,1892.
- Visual reference is C:\Users\bslac\Documents\ChatGPT\Done\vp0228-menu-free-cut-4191-4194.png. Earlier Eternals footage with player menus is an expansion-plus-UI control.
- Overlay fixtures are under C:\Users\bslac\Documents\ChatGPT\Done\ar-overlay-adversaries. Stronger soft, textured, tinted and picture-sided controls are generated in the paired replay.
- Existing experiments reside in E:\codex\videoprocessor\vp-0228-lookahead-20261110, primarily AnalysisDiagnosticReplayTests.cpp. Preserve local work and inspect its diff. The last verified remote beta tip was 3247ea13687ba5cf96bae6c84a883e875dca575f on v2.0.4-beta. Rediscover and fetch the current remote beta before implementation.
- Latest investigation passed 127 focused tests in x64 Release. These include experiment counterexamples and component tests. They do not certify a deployable accelerated detector or full live regression coverage.

## Acceptance and validation

- Demonstrate a measurable improvement in the clean Eternals transition, with correct source-derived boundaries and preserved picture/caption pixels. Report first detection, proof readiness, admission and actual presentation separately.
- Test dark and bright genuine transitions, scope returns, asymmetric bars, raised/variable black, gradients, full black/fades, sparse titles, dense/changing captions, volume/player UI and persistent/animated/tinted panels. Include saved forest, Karun, campfire, Ben-Hur and Netflix cases where available, with verified labels and bases.
- Compare unchanged beta and candidate on identical ordered sources. Report missed bars, false crops/expansions, boundary error, stability and delay. Examine false positives as well as the target scene. Historical component passes require revalidation for the combined candidate.
- Exercise history acquisition, expiration, contradictory observations and reset behavior, including an overlay matching remembered bounds. Validate exact frame/context association, gaps, repeats, missing/late analysis and disabled lookahead.
- Maintain normal live HDMI presentation with bounded optional worker work and no growing backlog. Preserve current-source frame/timestamp identity through fallback. Measure full critical-path cost and worst-case frame-budget behavior, distinct current submissions and presentation gaps under dense subtitles and slow analysis.
- Verify Off-mode stability and downstream integration while deriving detected AR independently of screen aspect, CIH, zoom, fitting, fill limits and NLS.
- Record the final rule, evidence, known limitations and build identity. Deploy through a completed x64 Release build with existing configuration values preserved, then verify live cadence and visible behavior.

## Related work

[VP-0228](../in-progress/VP-0228-diagnose-outward-picture-lookahead.md) owns the incident diagnostics and detailed replay evidence. [VP-0218](../in-progress/VP-0218_adaptive-aspect-boundary-evidence.md) contains prior adaptive boundary work and its unresolved dark/gradient cases. Reuse accepted implementations and reconcile overlapping scope before changing shared rules.
