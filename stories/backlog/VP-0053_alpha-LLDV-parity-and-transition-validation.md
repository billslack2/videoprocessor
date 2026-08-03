# VP-0053: Alpha LLDV parity and transition validation

## Status

Backlog. VP's existing shared LLDV effective-state path is implemented and is
already used successfully with DeckLink Quad HDMI Recorder plus madVR. Alpha
also receives that shared state. This story is limited to proving and, where
evidence requires it, completing Alpha parity with that existing behavior.

## User story

As an Alpha-renderer user with the existing DeckLink Quad and LLDV setup, I
want Alpha to consume the same effective LLDV state VP currently sends to
madVR and to transition cleanly between LLDV, HDR10, and SDR.

## Existing madVR contract to preserve

LLDV classification and effective source-state construction are shared VP
logic, not renderer-specific:

- the legacy HDFury path is raw BT.2020 + PQ without static HDR metadata;
- opt-in `newlldv` is a stable BT.2020 + SDR candidate without static metadata,
  gated by both LLDV-follow selections; and
- VP promotes a confirmed case to its existing effective PQ/BT.2020 state and
  supplies its configured synthetic static-HDR values.

madVR receives the effective primaries, matrix, and transfer in its DirectShow
media type and the mastering/content-light values as sample side data. This is
the baseline for this story. VP-0053 must not change LLDV classification,
defaults, overrides, or madVR delivery merely to create a different Alpha
interpretation.

Alpha already receives the same shared effective state as ordinary
BT.2020-NCL + PQ plus the static HDR fields in `pl_color_space`. The deployed
`C:\Videoprocessor\vp\logs\vp_debug.log` contains Alpha sessions at
approximately 18:25 and 18:30 on 2026-08-02 with the legacy VP LLDV values:
PQ/BT.2020, mastering 0.0001..1000 nits, and MaxCLL/MaxFALL 1000. This proves
the basic state reaches Alpha; transition behavior and full parity remain to be
validated.

## Scope

Use the available DeckLink Quad HDMI Recorder and the existing VP/HDFury LLDV
workflow. Compare Alpha with the current VP + madVR result and state contract.

This story includes only:

- confirming that Alpha receives and applies the same VP-built effective
  LLDV state as madVR;
- validating LLDV startup and LLDV/HDR10/SDR transitions on the Quad;
- adding focused raw-versus-effective state logging needed to prove parity;
- fixing an Alpha-specific state, cache/history, or transition defect only
  when the Quad test demonstrates it; and
- protecting existing madVR behavior with regression tests.

## Explicitly out of scope

- DeckLink Mini or any other capture-card validation;
- DeckLink SDK or Desktop Video upgrades;
- a current-SDK Dolby metadata probe;
- HDMI analyzer, AVI/DRM InfoFrame, or VSIF capture work;
- native Dolby Vision decode, RPU recovery, `PL_COLOR_SYSTEM_DOLBYVISION`, or
  `pl_dovi_metadata`;
- changing VP's LLDV heuristic, synthetic metadata defaults, HDFury/EDID
  target, or user configuration model;
- redesigning madVR's media type or sample-side-data path; and
- general DirectShow metadata-lifecycle work not needed for demonstrated Alpha
  parity.

DeckLink captures pixels after the source has performed player-led Dolby
mapping. VP's existing PQ/BT.2020 plus synthetic static-HDR treatment is the
contract being matched. The synthetic values are renderer hints, not recovered
Dolby metadata. That distinction is documented here only to prevent accidental
scope expansion.

## Required behavior

1. With the same VP configuration and Quad input, a confirmed LLDV state sent
   to madVR and Alpha has identical effective transfer, primaries, matrix,
   range, mastering values, MaxCLL, and MaxFALL.
2. Alpha treats that state as ordinary PQ/BT.2020 HDR input and tone maps it
   through its existing libplacebo pipeline.
3. Alpha must not add native-Dolby processing or reinterpret the shared values.
4. Starting Alpha while LLDV is already active produces the correct picture
   and state without a manual renderer restart.
5. SDR -> LLDV, LLDV -> SDR, and LLDV <-> HDR10 converge to the same effective
   states that VP supplies to madVR.
6. Returning to SDR clears effective PQ and synthetic HDR state without a stale
   LLDV frame, stuck HDR treatment, reset loop, or queue starvation.
7. A same-PQ/BT.2020 transition whose static HDR values change reaches the
   correct Alpha frame. If libplacebo tone/peak history causes observable stale
   behavior, flush only the necessary Alpha history at that boundary.
8. Alpha updates in place when its existing output/display-rule contract allows
   it. When that contract changes, use one bounded reconstruction through the
   existing coordinator.
9. Switching madVR -> Alpha while LLDV is active gives Alpha the current
   effective state rather than the raw capture state or a stale prior state.
10. Existing madVR LLDV behavior remains unchanged.

## Diagnostics

Log once per relevant transition:

- raw EOTF, primaries, and static-HDR presence from the Quad;
- LLDV classification result already produced by shared VP logic;
- complete effective state sent to the selected renderer;
- source/effective generation and renderer generation;
- Alpha's in-place, history-flush, or rebuild decision; and
- the final steady state.

The OSD must reflect the effective input treatment. It must not show SDR while
Alpha is processing VP's effective PQ state or retain PQ after return to SDR.
No new DeckLink capability-discovery subsystem is required.

## Quad validation matrix

| Condition | Required result |
| --- | --- |
| Capture hardware | DeckLink Quad HDMI Recorder only |
| madVR baseline on LLDV | Record effective state and expected visible treatment |
| Alpha starts on the same LLDV input | Same effective state; correct tone mapping without manual restart |
| SDR menu -> LLDV | One stable promotion; no stale SDR frame after convergence |
| LLDV -> SDR menu | SDR restored; no stuck PQ or synthetic metadata |
| LLDV -> HDR10 -> LLDV | Correct shared effective state at each boundary |
| Same PQ/BT.2020 with metadata change | Alpha applies new values at the correct frame boundary |
| 23.976/24 and 59.94/60 where available | No reset loop, starvation, or persistent stale frame |
| madVR -> Alpha during LLDV | Alpha receives the current shared effective state |
| Alpha -> madVR control | Existing madVR behavior remains unchanged |

Use the existing configured source and HDFury workflow. Preserve relevant
`C:\Videoprocessor\vp\logs\vp_debug.log` excerpts for the madVR baseline and
each Alpha test, including input state, effective values, renderer decision,
refresh, and generations.

## Implementation guidance

- Keep classification and effective-state ownership in shared VP logic.
- Do not create an Alpha-only LLDV heuristic or metadata policy.
- Compare complete `VideoState` values at the renderer boundary, not only the
  visual result.
- Keep the effective state attached atomically to its frame/generation.
- Include static HDR values in the Alpha transition comparison where needed to
  prevent stale tone/peak history across an effective-state boundary.
- Reuse the existing Alpha update/rebuild coordinator and stale-frame
  protection. Do not add independent timers or queue policies.
- Add focused tests for shared-state delivery and Alpha's
  accept/flush/rebuild decision before the Quad hardware run.
- Make no DirectShow/madVR change unless required solely to add non-invasive
  regression evidence.

## Acceptance criteria

- On the DeckLink Quad, Alpha receives the same VP-built LLDV effective state
  as madVR and tone maps it correctly through ordinary PQ/BT.2020 processing.
- Startup and tested LLDV/HDR10/SDR transitions reach the correct state without
  manual restart, loop, starvation, stale frame, or stuck HDR treatment.
- Metadata changes reach Alpha at the correct frame boundary without stale
  libplacebo tone/peak behavior.
- Alpha rebuilds only when its existing display/output contract requires it;
  otherwise it updates in place.
- Logs prove raw state, shared effective state, Alpha decision, and final state.
- Existing Quad + madVR LLDV behavior and values do not change.
- No Mini-card, SDK-upgrade, HDMI-metadata, native-Dolby, calibration, or
  metadata-policy work is introduced by VP-0053.

## References

- `src\VideoProcessor-GUI\VideoProcessorDlg.cpp`: existing LLDV classification
  and shared effective-state construction
- `src\VideoProcessor-Lib\blackmagic_decklink\BlackMagicDeckLinkCaptureDevice.cpp`:
  Quad capture EOTF/colorspace/static-HDR input
- `src\VideoProcessor-Lib\libplacebo\LibplaceboVideoRenderer.cpp`: Alpha source
  color translation, frame-state snapshot, and `OnVideoState`
- `src\VideoProcessor-Lib\microsoft_directshow\video_renderers\DirectShowGenericHDRVideoRenderer.cpp`:
  madVR effective-state delivery
- `src\VideoProcessor-Lib\microsoft_directshow\live_source_filter\ALiveSourceVideoOutputPin.cpp`:
  madVR static-HDR sample side data
- VP-0018: Alpha renderer refresh-rate reselection on switch
- VP-0041: Eliminate stale-frame flashes across renderer rebuilds
- VP-0045: Namespace built-in renderer configuration as `vpvr`
