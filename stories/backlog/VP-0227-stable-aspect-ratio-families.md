# VP-0227: Stabilize closely related aspect-ratio families

## Status

Backlog

Requested 2026-11-10. Next: inspect the then-current beta and its accepted measured-geometry, remembered-aspect, subtitle and OSD paths; reconcile the existing deadbands and family learning with this contract before implementation. No production change or deployment is included in this story.

## User story

As a viewer of live HDMI content, I want closely related aspect interpretations to remain stable across subtitles, minor signal variation and returns from other aspect ratios, so captions do not make the picture jump or cause the OSD to chatter while genuine mixed-aspect transitions remain responsive.

## Requested behavior

- Define two close-ratio families for stabilization and presentation naming: **16:9 and 1.85:1**, and **2.35:1 and 2.39:1**. These are distinct from 1.90:1 IMAX and other genuinely different ratios; do not merge 1.85 and 1.90 merely because the existing remembered-family helper currently does so. Verify exact eligible ranges and ambiguous boundaries from real measurements and regression evidence before coding, rather than assuming a standard ratio is a measured boundary.
- Once a supported representative of a family has been accepted for the current source context, retain that representative against insignificant same-family variation. Subtitle onset, disappearance, movement or uncertainty must not alone switch between family members or shift the visible picture. Continue to move/reconstruct captions as configured, including their backing and padding, without clipping picture or captions. A subtitle is not itself authority for a new crop or an absence of picture.
- In a mixed-aspect title, a correctly evidenced transition out of a family—for example scope to 1.90:1—must proceed normally. On return to the family, prefer its previously accepted representative when current source evidence is consistent and the remembered context is still valid. If no family representative is known, use the available evidence and the existing safe uncertainty rules. Memory must not prevent a genuine change, hide newly visible picture or overrule contradictory current evidence.
- Do not present 21:9 or 2.40:1 as separate OSD choices. For conventional **labeling**, treat the former as 2.35:1 and the latter as 2.39:1, subject to accurate measurement and safe ambiguity handling. Preserve the numeric measured ratio and exact source bounds independently where exposed; these naming aliases must not fabricate or force a 2.35 or 2.39 crop.
- Make family retention compatible with the existing geometry and aspect deadbands, transition confirmations, remembered-geometry constraints and subtitle stability. Audit actual current code and interactions first; do not stack overlapping delays or change established subtitle/AR architecture wholesale. Define how family memory is scoped, established, corrected and cleared across source changes, seeks, input/format changes and renderer resets; stale history must never be applied to unrelated content.
- Detection must continue to derive the actual active picture and aspect from source pixels, independent of screen size, CIH, zoom, fitting, fill limits or NLS. Family membership and labels cannot invent positive picture/bar evidence, suppress valid outward expansion, or turn unknown/provisional observations into detected authority. Preserve nonstandard and genuinely distinct ratios.
- Support live HDMI noise, chroma fluctuations, compression and a few pixels of transfer variation using evidence and existing safeguards, not pixel-perfect or one-frame thresholds. SDR, HDR and Dolby Vision must share the decision contract with format-appropriate sample interpretation.
- Do not block, replay or hold an old video frame while awaiting family, subtitle or crop decisions. Keep current-source submissions, frame cadence and latency bounded, and use a pixel-preserving current-source fallback when optional evidence is unavailable.

## Acceptance and validation

- Tests cover stable 16:9/1.85 and 2.35/2.39 families with small boundary jitter; retained family representative and visible picture geometry remain stable when captions appear and disappear, including partly in-picture captions and black backing. Test subtitle Off, Classic and advanced modes where supported.
- Exercise 2.35/2.39 → 1.90 → scope with and without prior family memory, and the corresponding 16:9/1.85 cases. Genuine current evidence can establish a different geometry; valid visible picture and captions are never lost to an old representative. Verify resets, source changes, seeks, delayed/out-of-order evidence, and memory correction/expiry.
- Verify 21:9 and 2.40 are not shown as distinct labels, aliases produce the intended 2.35/2.39 names, and underlying measured bounds/aspect remain truthful. Include nonstandard ratios and negatives around the family limits, including 1.90 IMAX and 2.76 Ben-Hur.
- Compare unchanged-baseline and changed behavior on available moving and paused Eternals, LOTR and Ben-Hur captures, including real caption-on/off sequences and compression/chroma variation. Distinguish detected geometry, presentation geometry and OSD labels; do not use a screen-filling result as proof of correct detection.
- Test existing deadband and transition regressions, subtitle reconstruction and current-source frame progression for SDR/HDR/DV. Check playback and timing on actual hardware where available; report source/presentation gaps, critical-path worst-case frame-budget cost and bounded latency, not just renderer drop counters. Record any missing live qualification explicitly.
- Coordinate confirmed aspect-change notifications with [VP-0226](VP-0226-confirmed-aspect-action.md): family label stabilization must not manufacture, suppress or duplicate confirmed measured-aspect events.

## Existing-code context

At the 2026-11-10 integration review, `ActivePictureTransitionModel` had guarded remembered-aspect families and separate stable-geometry/aspect deadbands; `AspectRatioOsd.h` still named 21:9 and 2.40 separately. These observations are a starting point only: re-check the current beta and actual tests before implementing. Related detection and transition work is tracked in [VP-0218](../in-progress/VP-0218_adaptive-aspect-boundary-evidence.md) and [VP-0136](../in-progress/VP-0136_prevent-transient-same-axis-inward-aspect-switches.md).
