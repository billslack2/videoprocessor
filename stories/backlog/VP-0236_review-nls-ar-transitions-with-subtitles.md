# VP-0236 Review NLS across aspect-ratio changes with subtitles enabled

## Status

Backlog

## Viewer question

On a 2.35:1 constant-image-height screen, when Eternals changes between scope, IMAX and 16:9, does nonlinear stretch engage or release once for the new picture, and do captions remain readable and stable through that handoff?

## Starting evidence

The November 19, 2026 playback log at 10:44:21 AM Eastern contains a usable transition. Find it by timestamp and generation in the deployed logs directory, since filenames rotate. At review time it was in C:/Videoprocessor/vp/logs/vp.log.0.

- Generation 1, sources 5363 through 5370 show NLS requested, a subtitle-retention base of y=276..1884 in a 3840x2160 raster, and routed subtitle shifts of 100 then 114 pixels.
- At source 5375 the base changes to y=68..2092 and routed shift returns to zero. The same-source renderer layout reports content aspect 1.897233, NLS active, hook active and rendered, with a 2.35 screen and zero reported screen margins. This establishes a renderer decision. Caption visibility and actual screen timing still need corroboration.

## Review and acceptance

1. Reconstruct the transition by source generation and sequence. Record input raster, measured picture bounds, accepted crop, subtitle cue and shift, NLS request and eligibility, actual hook and final output geometry. Identify the first frame where each changes. Use the configured profile and fill limits to explain the expected handoff.
2. Compare a scope expansion and its reverse, and include 16:9 where evidence permits. Check a cue spanning the change, cue arrival or removal with stable AR, and an otherwise comparable subtitle-off or NLS-off baseline. Look for repeated engagement, unexpected fallback or four-sided bars, subtitle clipping, duplication or warping, and video stalls.
3. Report each observed issue with source identities and the earliest responsible stage. Verify whether a subtitle-only event changed physical detection or simply moved the presentation. Check source progression and presentation gaps around the event.
4. Use existing logs first. If an important case lacks evidence, specify one short visible-output recording with matching input-frame diagnostics and playback time. Record verified behavior, outstanding evidence and any narrowly scoped follow-up. Visual timing and glyph placement require visible output.

## Related work

VP-0230 covers physical detection. VP-0190 covers NLS-only profile changes, VP-0196 covers anamorphic layout, and VP-0235 covers switching subtitle profiles. This review keeps the selected profile fixed and examines content-driven AR and cue changes during playback.
