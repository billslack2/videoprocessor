# VP-0189 tester brief: automatic crop in dark and multi-AR scenes

We have reproduced crop-policy problems that can cause rapid crop/full-frame
switching and prevent crop from returning after a release. We are preparing
targeted fixes and additional diagnostics. These reproductions do not yet prove
the cause of your particular incident, and we have not established a fix for
every star-field case.

Could you send the original complete VP debug log from an affected viewing
session, if you still have it? Logs from the existing build are useful now; you
do not need to wait for a candidate build to report a repeatable passage.

Please include:

- VP version/build ID and source player/device; disc/streaming edition and
  input resolution/frame rate if known.
- Film/episode title and approximate playback timestamp or chapter. A PC clock
  time, including time zone, helps match the visible event to the log.
- What happened: repeated zooming, bars returning and staying, failure to crop,
  or actual picture/stars being cut off. Estimate its duration and whether it
  recovered by itself.
- Whether subtitles were on, any player/receiver menu or volume overlay was
  visible, and whether you changed profiles, paused or sought near the event.
- The selected Screen/Zoom profiles and whether automatic crop, subtitle fit
  and NLS were enabled. Relevant settings or screenshots are enough; a complete
  configuration file is not required initially.
- Whether the passage can be reproduced, especially a star field with a real
  aspect-ratio change. Please identify which part should legitimately fill more
  of the frame if you can tell.

Please send the complete raw debug log, not only filtered lines or an AI
summary. Include the numbered/rotated files covering the session and preserve
them before repeated restarts overwrite older logs. Use the debug-log folder
configured for your installation; filenames and locations can differ.

For a new reproduction, start playback roughly 30-60 seconds before the event
so crop can settle, then let it continue through recovery or for at least a
minute afterward. Note playback time and PC time when the problem appears.
One short, repeatable passage is more useful than an unrelated long viewing
session. Do not change crop thresholds during the comparison.

When a candidate package is supplied, it will identify the exact build and
include instructions for enabling its bounded crop diagnostics. Repeat the
same passage/settings and return logs even if the picture looks fixed. Also
check ordinary stable bars, a genuine transition to full height, return to
letterbox, and subtitles if available. Report any delayed legitimate aspect
change or newly clipped stars/picture, as well as reduced flicker.

This brief is a request for evidence, not a release announcement. No VP-0189
candidate binary or new logging switch is available yet.
