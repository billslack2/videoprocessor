# VP-0199: Remove residual subtitle backing at the picture boundary for small fonts

## Status

Backlog

Created 2026-10-14 at the user's request. Implementation and validation have not started.

## User story

As a VP user, I want the complete original subtitle backing removed and its picture-area portion repaired when subtitles are relocated or reduced, so small fonts do not leave a thin black strip protruding above the letterbox bar.

## Observed issue and evidence

The user reports this often occurs with smaller subtitle fonts. The supplied example shows relocated yellow text above a narrow rounded black strip at the lower picture boundary. The enlarged screenshot marks the residual strip in red.

- [Full screenshot](../assets/VP-0199/small-subtitle-backing-overview.png)
- [Highlighted detail](../assets/VP-0199/small-subtitle-backing-detail.png)

An incomplete original-backing cleanup boundary is a working hypothesis, not a confirmed root cause. Determine whether the residual belongs to the source backing, the generated overlay, rounding, sampling, or repair coverage before changing policy. Screenshots show the symptom; native input and processing coordinates are needed to establish the cause.

## Scope and proposed approach

- Track the original confirmed subtitle backing independently of the replacement text size and destination rectangle.
- When backing is confirmed, include its full picture-area extent in cleanup, even if only a few pixels protrude above the bar and no glyph crosses the bar boundary.
- Include rounded corners and any supported antialiased boundary fringe; use bounded evidence-based expansion rather than arbitrary padding into real picture content.
- Keep background repair samples outside the confirmed contaminated area.
- Preserve existing glyph evidence, edge proximity, source identity and temporal safeguards. Do not broaden general subtitle detection merely to cover this strip.
- Preserve AR authority and avoid introducing repair shimmer, flicker, or new crop changes.

## Acceptance criteria

1. A reproducible regression demonstrates the residual strip before the fix and complete cleanup afterward.
2. Small fonts and reduced replacement text do not shrink the original backing removal mask.
3. Shallow backing intrusion, rounded corners, fractional/scaled boundaries, and single/multiple subtitle lines are handled without visible residual black edges.
4. Repair changes only confirmed contaminated picture pixels; nearby genuine dark scene content remains intact.
5. Equivalent top-edge cases and supported DV/non-DV analysis paths behave consistently.
6. Existing subtitle detection, background repair, temporal stability, and AR regression suites remain passing; paused and moving playback confirm no new flicker or shimmer.

## Validation plan

Create production-connected fixtures for small glyphs with a large backing, one-to-few-pixel picture intrusion, rounded/antialiased corners, subtitle text entirely inside the bar, and different source/output scales. Include negative controls with dark scene shapes near the edge and no confirmed subtitle. Cover cue transitions and changes in font or replacement-text size. Record native coordinates and cleanup/sample masks so a visually similar screenshot cannot hide a mask mismatch.

## Next action

Capture or reproduce the source and compare detected glyph bounds, original backing bounds, removal coverage, destination overlay and repair sampling. Select the smallest safe correction supported by those measurements.
