False subtitle flash investigation, 2026-10-09

Recording: 2026-10-09 22-35-01.mp4, 60 fps desktop recording.
Logs: matching-log.txt, native capture 23.976 PQ BT.2020; 16 accepted cue IDs
25..40 around 22:35:07-08, source picture boundaries unchanged at 276..1884.

The desktop recording contains a tone-mapped, scaled, already-composed result.
41 inverse-resized desktop frames (4.5..8.5 sec) reject before and after the fix.
They are negative safety controls, NOT a reproduction of the native HDR admission.

A controlled P010 regression demonstrates the concrete code defect: proximity
increments topBarInk/bottomBarInk for nomination, but those counters also bypassed
the bright-core check. A centered 400-luma fragment survives the low 376 mask floor
while the learned core level is 510. Before: detected=1; after: detected=0.
The new native unit test mirrors the case at both bars and proves an actual 510
core is still accepted. Only physical actualBarInk may use the existing exemption.

All 8 real dark-scene caption frames in real-caption-results.json detect.
Live HDR replay remains needed to establish whether this one bypass accounts for
all flashes; no request for a new user recording is required.
