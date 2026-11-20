# VP-0237 Investigate and reduce late subtitle appearance at 23.976 fps

## Status

Backlog

## Observation

During 23.976 fps playback on November 19, 2026, several subtitle cues appeared late and corrected themselves shortly afterward. The deployed session used the Classic subtitle profile. The exact cue timestamps and the stage responsible for the delay are unverified.

## Investigation

1. Identify the affected title, cue positions and playback session from logs and available evidence. If the existing record cannot identify source cue onset, capture a short reproducible section with synchronized HDMI input and visible VP output.
2. Trace each cue from the first HDMI source frame containing it through worker admission and completion, caption evidence, composition, renderer submission and actual presentation. Correlate frame identity and timestamps. Measure onset delay in milliseconds and source frames. Check worker deadlines, stale results, cadence and repeats, AR or caption-fit transitions, and first detection versus verified reuse. Use a timely cue for comparison.
3. Compare under matched settings with a pinned earlier release where subtitles worked well. Record input and output cadence, stage costs, caption timing and frame progression.
4. Fix the earliest verified cause. Keep expensive caption work on the bounded worker path. Keep current video advancing when optional work is late, with pixel-preserving fallback and no reuse of old movie or cleanup pixels.

## Acceptance

- Identify the earliest stage responsible for each reproducible delay and report before-and-after cue onset and placement timing against the source.
- Verify short, dense, changing and sustained cues, including stable AR and AR transitions. Check caption preservation and absence of duplicates or stale pixels.
- Measure current-source submissions, actual presentation gaps, worst-case frame-critical cost and bounded capture-to-display latency. The fix must improve cue timing without slowing or freezing video.

## Related work

VP-0232 covers moving subtitle verification off the renderer thread. VP-0234 covers the live critical path. VP-0236 covers NLS and AR transitions with subtitles enabled.
