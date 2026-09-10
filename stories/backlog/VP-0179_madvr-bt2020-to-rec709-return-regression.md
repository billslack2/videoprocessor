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

## Investigation update (2026-09-10)

Inspected the freshly fetched GitHub beta `v1.3.005-beta` at `89d55ca5`
(Merge PR #84), in clean detached worktree
`E:\codex\videoprocessor\vp-0179-investigation`. No source edits, build,
deployment or hardware reproduction performed. Story remains backlog pending
incident evidence. The linked AVSForum post could not be retrieved during this
pass; the report above is not independently verified.

### Established source behavior

- `DirectShowVideoRenderer.cpp:129-175` rejects changes to colorspace or EOTF
  on both the caller admission path and graph-thread path. The GUI rejection
  handler (`VideoProcessorDlg.cpp:5995-6055`) schedules a restart, with explicit
  LLDV and pending EOTF stabilization exceptions. Thus a constant-EOTF
  BT.2020 -> Rec.709 change is not generally ignored.
- `CaptureVideoStatePolicy.h:36-59` includes colorspace and EOTF in the material
  contract. The metadata-only ingress retention change `f9242793` does not
  classify a real colorspace change as metadata-only.
- `DirectShowGenericHDRVideoRenderer.cpp:473-486` derives media-type primaries,
  matrix and transfer function from effective state unless forced. Confirm
  those overrides before attributing output to raw capture metadata.

### Concrete gaps, not a reproduced root cause

1. `BlackMagicDeckLinkCaptureDevice.cpp:751-772`: unsuccessful EOTF or
   colorspace GetInt calls leave the cached value unchanged; loss of the
   metadata interface also skips those updates. If return-to-menu stops
   reporting these properties, VP can retain its previous raw contract.
   Blame dates this read-success-only behavior to `d71bde25d` (2021), so it
   is not evidence of a newly introduced regression. Capture each HRESULT,
   metadata-interface availability, frame flags and cached values to test it.
2. `DirectShowGenericHDRVideoRenderer.cpp:140-141` skips null HDR updates;
   `ALiveSourceVideoOutputPin.cpp:540-548` explicitly rejects null HDR data.
   Therefore metadata withdrawal with an otherwise unchanged contract does
   not clear the existing source's cached HDR block. The asynchronous guard
   dates to `ec59c0b85` (2026-07-29); the synchronous non-null-only behavior
   is older. This is relevant to metadata-only transitions, but does not by
   itself explain failure after a successful Rec.709 graph replacement.
   Removing the guard alone would hit the source's null rejection.

### Available runtime evidence and next discriminator

The configured guidance path `C:\logs\vp.log` is absent. Inspected available
`C:\Videoprocessor\vp\logs\vp.log` and rotations `.0` through `.8` instead.
They contain SDR EOTF baselines and madVR runtime examples with
`yuv_matrix='TV.709'`, `hdr_output=0`; the targeted searches found no pending
EOTF transition or BT.2020 raw-state evidence establishing this incident.
These are local-session observations, not the reporter's failing capture or
proof of HDR-to-SDR recovery.

Obtain exact good/failing source revisions and the same CPU-conversion setup.
For one reproduced exit, determine the first failing boundary:

- Raw VP remains BT.2020: inspect DeckLink read results and source HDMI state.
- Raw is Rec.709, effective remains BT.2020/PQ: inspect forced settings, active
  profiles and LLDV decision traces.
- Effective is Rec.709, madVR remains BT.2020: inspect rejection, deferred
  restart, replacement generation and negotiated media type.
- madVR reports TV.709/SDR but the display remains BT.2020: obtain receiver /
  projector signaling evidence; matrix alone does not prove HDMI primaries.

Do not select a bisect endpoint from the July 31 calendar date or implement a
speculative metadata reset before identifying the failing boundary.
