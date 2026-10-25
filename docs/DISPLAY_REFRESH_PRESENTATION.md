# Presentation on every display refresh

In Config, open Display and enable **Present on every display refresh**. This is
an opt-in VP Renderer setting, shared by display profiles, and defaults to off.
Apply restarts VP Renderer. It is saved as `[general] present_every_vsync: true`.
DirectShow and madVR keep their own presentation settings.

Ordinary progressive output uses the measured output refresh rate for submission
pacing. Source timestamps select the next due image. Between new source frames,
VP copies the final processed output into the current backbuffer and submits it
with vsync. This preserves the rendered pixels, overlays and dithering without
rerunning source conversion, tone mapping, crop or subtitle analysis. The cache
owns GPU storage and retains no capture buffers. A 24 fps source on 120 Hz has
five presentations per source frame; 24 fps on 60 Hz retains the 3:2 cadence.

The input source is never sped up or slowed down. Already due buffered source
frames may be retired while preserving the configured queue target. Missed
refresh deadlines are skipped rather than repaid in a burst. Source resets,
discontinuities, profile changes, shader changes, resize and output negotiation
invalidate cached output. An unavailable refresh rate falls back to normal
source-paced presentation. Repeats and retired source frames are recorded in the
`VP display refresh` diagnostic log.

Smooth motion remains independently controlled under Motion. When temporal
rendering is active, its own frame selection remains authoritative; the display
option also asks deinterlacing to schedule at the display refresh rate. Blending
is never enabled by this option. Live HDMI qualification is still needed to
measure cadence, GPU copy cost and latency on the target display.
