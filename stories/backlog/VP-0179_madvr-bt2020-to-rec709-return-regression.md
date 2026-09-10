# VP-0179: madVR BT.2020-to-Rec.709 return regression

## Status

Backlog (2026-09-10). Reporter says recent builds fail to return from BT.2020
to Rec.709 after HDR playback; a build based on July 31, 2026 works. Exact
known-good/failing SHAs, converter patch, configuration and transition logs
are missing. This is a separate incident from conversion CPU performance.
No reproduced root cause or fix is claimed.

## User story

As a madVR user returning from HDR content to a Rec.709 menu/SDR content, I
want VP and madVR to update their input/output color contracts reliably,
without restarting the application or staying latched in BT.2020.

## Evidence and scope

User supplied cinillo2's report:
https://www.avsforum.com/goto/post?id=64830993
The user also reports faster rendering on an older version. GPU conversion
was built on the older working baseline, so it is a confounder and is not
evidence that the converter fixes color signaling. Compare native CPU builds
first, with the same configuration and source sequence.

Initial read-only source inspection used current deployed 04d3988c, based on
explicitly requested local VP-0174 16a8102f. DirectShow media type creation
sets transfer matrix, primaries and transfer function from effective video
state unless overridden. BuildPushVideoState and renderer rejection can cause
a restart for material state changes. VP-0170 separately stabilizes raw EOTF
with two matching valid observations across a 5000 ms window. This policy
tracks EOTF, not primaries, but a separate rejection/restart path exists;
its presence must be accounted for before claiming colorspace-only changes
are ignored. Raw versus effective LLDV EOTF/primaries need separate tracing.

## Investigation and acceptance

1. Obtain exact good and failing builds, source/receiver/display chain, madVR
   version/settings and VP configuration. Identify whether 'stuck BT.2020'
   refers to VP input OSD, madVR input matrix/primaries, projector information,
   or visibly incorrect colors. These identify different failure boundaries.
2. Capture logs beginning before HDR playback ends through at least 10 seconds
   of the SDR menu: raw DeckLink EOTF/colorspace/HDR block, effective overridden
   state, LLDV state, EOTF candidate/commit, renderer generation/restart and
   madVR runtime input/output status. Record HDMI display evidence separately.
3. Reproduce on matching CPU-converter builds/configuration and bisect between
   verified source revisions. Do not infer a source SHA from a calendar date.
4. Test PQ/BT.2020 -> SDR/Rec.709, SDR/BT.2020 -> SDR/Rec.709 (constant EOTF),
   transient metadata withdrawal, LLDV -> menu, and unchanged-resolution/rate
   transitions. Cover conversion NONE and P010 to isolate negotiation effects.
5. Confirm stable Rec.709 input reaches the intended media type/madVR state,
   stale HDR metadata clears, forced user overrides remain deliberate, and
   necessary graph rebuild occurs once without restart loops. Verify display
   signaling only with receiver-side evidence, not driver acceptance alone.
6. Implement only after the failing boundary is established; add regression
   tests for that boundary and perform repeated hardware HDR-to-SDR cycles.

## Related work, not duplicates

- VP-0170: EOTF transition stabilization (merged, hardware acceptance pending).
  This report is a distinct return-to-Rec.709 incident and may expose a gap;
  cross-link rather than assume VP-0170 caused or fixed it.
- VP-0151: VP Renderer NVAPI AVI InfoFrame exit, explicitly excluding
  DirectShow signaling. Do not merge this madVR report into that story without
  evidence that VP Renderer previously owned the display state in this session.
- VP-0178: CPU/GPU conversion investigation, deliberately separate.

## Source inspection locations

- src/VideoProcessor-GUI/VideoProcessorDlg.cpp: capture state, BuildPushVideoState,
  ObserveEotfTransition, LLDV exit and deferred renderer restart handling.
- src/VideoProcessor-Lib/EotfTransitionStabilizer.h/.cpp
- src/VideoProcessor-Lib/microsoft_directshow/video_renderers/DirectShowGenericHDRVideoRenderer.cpp
- src/VideoProcessor-Lib/microsoft_directshow/DirectShowTranslations.cpp
