# VP-0053: Alpha LLDV parity and transition validation

## Status

Backlog. The shared effective-state mechanism already exists, and deployed
Alpha logs prove the legacy HDFury LLDV path reaches libplacebo as synthetic
PQ/BT.2020. This story is validation, observability, metadata-policy, and
transition hardening rather than a new native-Dolby implementation.

## User story

As an Alpha-renderer user, I want configured player-led Dolby Vision (LLDV)
input to be interpreted and tone mapped correctly through content, menu,
HDMI-resync, refresh, renderer, and display-profile transitions, so no HDR/PQ
treatment, stale image, or queue failure persists after the source changes.

## Existing contract and evidence

LLDV classification and effective source-state construction are shared VP
logic, not renderer-specific:

- the legacy HDFury path is raw BT.2020 + PQ without static HDR metadata;
- opt-in `newlldv` is a stable BT.2020 + SDR candidate without static metadata,
  gated by both LLDV-follow selections; and
- confirmed LLDV becomes an effective PQ/BT.2020 state with an assumed static
  luminance envelope.

madVR receives the effective primaries/matrix/transfer in its DirectShow media
type and mastering/content-light values as sample side data. Alpha receives the
same shared effective state as ordinary BT.2020-NCL + PQ plus HDR luminance
fields in `pl_color_space`. Alpha can accept an EOTF/primaries-only transition
in place when its selected display rule does not change; a display-rule or
output-contract change may require one renderer rebuild.

The deployed `C:\Videoprocessor\vp\logs\vp_debug.log` contains Alpha sessions
with legacy synthetic LLDV values (PQ/BT.2020, mastering 0.0001..1000 nits,
MaxCLL/MaxFALL 1000) at approximately 18:25 and 18:30 on 2026-08-02. This
proves delivery and tone-map entry, but not classification, transition, or
value correctness.

## DeckLink hardware and SDK boundary

VP builds against DeckLink API 12.4 headers obtained with the SDK 12 package.
The deployed evidence is a DeckLink Quad HDMI Recorder with Desktop Video
driver 14.4.1. The exact DeckLink Mini Recorder variant(s) used by operators
must be recorded and tested separately; `Mini Recorder`, `Mini Recorder 4K`,
and other Mini models must not be treated as one capability.

SDK 12 already exposes the complete documented input contract VP uses:

- advertise SDR, static PQ, and static HLG through
  `IDeckLinkHDMIInputEDID`;
- read EOTF, colorspace, mastering primaries/luminance, MaxCLL, and MaxFALL
  through `IDeckLinkVideoFrameMetadataExtensions`; and
- test the frame's static-HDR flag.

The current DeckLink SDK retains that static-HDR capture model. It documents
Dolby Vision separately as an **output/playback** feature, does not document a
Dolby Vision input EDID mode or capture procedure, and limits ancillary packet
capture to non-HDMI transports. The Quad HDMI Recorder specification promises
HDMI 2.0b, deep-color/HDR acceptance, and Rec.2020, but not Dolby Vision
capture or raw AVI/DRM/VSIF access. Therefore upgrading VP from SDK 12 alone is
not an LLDV solution for Quad/Mini capture.

Before a production SDK migration, a standalone current-SDK capture probe may
test each exact Quad/Mini card and installed driver. It must query runtime
HDR/colorspace capabilities, log raw frame flags and metadata-query results,
and, where newer headers permit, attempt the Dolby metadata ID without assuming
input support. Correlate SDR, HDR10, LLDV, and TV-led DV with an HDFury or HDMI
analyzer showing the actual AVI/DRM/VSIF packets. A negative result confirms
the documented boundary; an unexpected positive result requires a separate
evidence-backed story and ownership/synchronization design.

## Scope boundary: effective HDR treatment, not native Dolby Vision

The source performs player-led Dolby composition/display mapping before
DeckLink captures the uncompressed pixels. DeckLink does not give VP the
original per-frame Dolby Vision RPU. Its documented API also does not expose
the HDMI Dolby VSIF on Quad HDMI capture. VP therefore cannot reconstruct
Dolby dynamic metadata, original trims, mastering signal, or authentic content
MaxCLL/MaxFALL.

Once explicitly configured policy confirms an LLDV candidate, VP interprets
the captured pixels as PQ/BT.2020 and supplies an assumed static luminance
envelope so renderers treat the signal as HDR. This is sufficient for the
post-mapped HDR-to-SDR workflow but is not native Dolby Vision decode.

libplacebo supports native Dolby Vision when a decoder supplies
`PL_COLOR_SYSTEM_DOLBYVISION` and `pl_dovi_metadata` parsed from an RPU. mpv
and MPC Video Renderer use that decoder-side-data model; it is not analogous to
VP's post-HDMI capture. Setting the Dolby color system for already player-mapped
LLDV pixels would be the wrong pipeline stage and risks double processing.
Even obtaining VSIF would improve classification only; it would not recover the
RPU. A future native-Dolby story requires compressed-stream/RPU access before
HDMI or hardware exposing a materially different input contract.

## Assumed luminance-envelope policy

Do not call VP's values "LLDV metadata." They are renderer hints describing an
assumed envelope for already source-mapped PQ pixels, not captured Dolby
metadata, VSIF, RPU, or authentic HDR10 mastering/content metadata.

Existing `newlldv` defaults mix mastering maximum 4000 nits with MaxCLL 1000
and MaxFALL 401. The legacy path uses a 1000-nit mastering maximum and
MaxCLL/MaxFALL 1000. libplacebo primarily uses mastering min/max to guide tone
mapping and uses MaxCLL only as a fallback if mastering maximum is absent.
Alpha can therefore interpret the current `newlldv` envelope as 4000 nits
despite MaxCLL 1000. Do not bless or silently normalize these values without
calibration evidence.

The preferred model is a named, logged LLDV target min/max matching the Dolby
VSVDB/EDID target advertised by the HDFury or equivalent device. Use that
assumed envelope in the shared effective state. Keep renderer-specific
compatibility adaptation at the renderer boundary, and document any required
nonzero madVR placeholders rather than presenting them as recovered source
facts.

## Required behavior

1. With `newlldv` and both LLDV-follow modes enabled, Alpha promotes only a
   stable configured BT.2020 + SDR candidate to effective PQ.
2. Treat the promotion as explicit configured interpretation, not automatic
   detection. Genuine SDR BT.2020 remains SDR unless that source policy is
   deliberately enabled; the 1.5-second timer prevents flapping but is not
   evidence of Dolby Vision.
3. Alpha receives effective PQ, BT.2020-NCL, and the named assumed target
   envelope before rendering confirmed LLDV.
4. Alpha processes LLDV through ordinary HDR-input tone mapping and gamut
   conversion. It must not use libplacebo's native-Dolby path or double map it.
5. Returning to an SDR menu restores real SDR treatment promptly. Effective
   PQ, the assumed envelope, a conditional display rule, and stale LLDV frames
   must not persist.
6. SDR -> LLDV, LLDV -> SDR, LLDV <-> HDR10, and refresh/HDMI-resync
   transitions converge without manual restart, reset loop, queue starvation,
   or dropped-frame burst.
7. A metadata-only LLDV/HDR10 boundary that remains PQ/BT.2020 updates the HDR
   envelope at the exact frame boundary and resets Alpha peak/tone history if
   it would otherwise span incompatible assumptions.
8. DirectShow explicitly clears obsolete HDR sample side data. Same-EOTF
   metadata disappearance must not retain previous mastering/CLL values.
9. If Alpha accepts the state in place, do not restart it. If an output or
   display-rule contract changes, perform one bounded reconstruction through
   the existing coordinator.
10. Switching Alpha and madVR while LLDV is active gives the incoming renderer
    the current effective state, never stale raw SDR/PQ state.
11. NLS/shader, viewport, OSD, and display-profile hotkeys do not clear or
    reclassify an active LLDV state.

## Diagnostics

Log once per capability discovery or state transition:

- DeckLink model/subdevice, API and Desktop Video versions, runtime
  HDR/colorspace capability flags, EDID interface/write result, raw frame
  flags, and metadata-query success/failure;
- raw EOTF/primaries and static-HDR presence;
- heuristic eligibility, stabilization, confirmation, cancellation, and
  rejection reason;
- explicit provenance (`captured_static_hdr`, `assumed_legacy_lldv`,
  `assumed_newlldv`, or ordinary SDR), configured evidence, effective state,
  assumed target envelope, and metadata fingerprint;
- source/effective generation and renderer generation;
- Alpha's in-place, history-flush, or rebuild decision and display rule; and
- final steady state.

The OSD must show effective input treatment without claiming native Dolby
Vision. It must not say SDR while Alpha is using assumed PQ treatment or retain
HDR/PQ after the source returns to SDR.

## Validation matrix

| Condition | Required result |
| --- | --- |
| Alpha starts on LLDV | Stable promotion and correct tone mapping without manual restart |
| SDR menu to LLDV | One promotion after stabilization, no false early promotion |
| LLDV to SDR menu | SDR restored; no stuck HDR/PQ or stale frame |
| LLDV to HDR10 and back | Correct effective state and envelope each time |
| Metadata-only PQ/BT.2020 boundary | New envelope at exact frame; Alpha history flushed if required |
| 23.976/24 and 59.94/60 LLDV | Correct behavior across refresh switch/resync |
| Each Alpha display/LUT rule | Correct rule selection and color contract |
| Alpha <-> madVR while LLDV active | Incoming renderer receives current effective state |
| Shader/NLS and viewport hotkeys | LLDV state remains intact |
| Genuine SDR BT.2020 | Never promoted without explicit configured policy |
| Dropout/resync during LLDV | Recovery without reset loop or starvation |
| Quad HDMI Recorder | Capability log plus SDR, HDR10, legacy LLDV, `newlldv`, and transition evidence |
| Each exact Mini Recorder variant | Independent capability/result row; no inheritance from Quad or another Mini |

Use NVIDIA Shield and Apple TV cases where available. Preserve relevant
`C:\Videoprocessor\vp\logs\vp_debug.log` excerpts for each test, including
source, refresh, display rule, policy/provenance, envelope, and generations.
Validate PQ ramps/color patches and at least two known Dolby scenes against a
direct LLDV reference. Include a source SDR menu as the false-positive case.

## Implementation guidance

- Keep classification and effective-state ownership in shared VP logic; do not
  create an Alpha-only LLDV heuristic.
- Give each effective state a policy/provenance, metadata fingerprint, and
  generation that travels atomically with its frame.
- Preserve captured range, matrix, format, and bit depth. Common LLDV may be
  12-bit 4:2:2; a DeckLink or VP conversion to 10 bit is a precision decision,
  not a metadata solution.
- Include the HDR metadata fingerprint in Alpha's transition/cache-flush
  decision. Verify whether libplacebo peak/tone history must reset for a
  same-EOTF/primaries envelope change.
- Add an explicit DirectShow side-data clear path for same-EOTF metadata
  disappearance. Do not rely only on LLDV-to-SDR graph reconstruction.
- During `newlldv` stabilization, measure whether DirectShow rebuilds once for
  raw BT.2020 SDR and again for promoted PQ. Consolidate only with evidence.
- Do not set `PL_COLOR_SYSTEM_DOLBYVISION`, create `pl_dovi_metadata`, or infer
  dynamic Dolby metadata from VP's assumed envelope.
- Reuse the reset/rebuild coordinator and stale-frame protection. Do not add
  independent timers or queue policies.
- Add focused tests for effective-state construction, provenance/fingerprint,
  Alpha accept/flush/rebuild decisions, and DirectShow side-data clearing
  before hardware transition validation.

## Acceptance criteria

- Alpha consumes confirmed LLDV as PQ/BT.2020 with a named, logged, calibrated
  assumed target envelope and tone maps it correctly.
- All tested LLDV, HDR10, and SDR transitions reach the correct state without
  manual restart, loop, starvation, dropped-frame burst, stale frame, or stuck
  HDR treatment.
- Metadata-only boundaries update atomically; Alpha history and DirectShow
  side data cannot retain obsolete HDR assumptions.
- Alpha rebuilds only when its display/output contract requires it; otherwise
  it updates in place.
- Genuine SDR BT.2020 is never reinterpreted without explicit configured
  policy.
- Logs and OSD make hardware capability, raw state, interpretation policy,
  effective state, envelope, renderer decision, and final state diagnosable.
- The story records exact Quad/Mini model, SDK/API, driver, runtime capability,
  and HDMI-analyzer evidence. It does not infer input support from newer
  output-only Dolby enums.
- Existing madVR LLDV, Alpha color/LUT, queue, refresh, shader/NLS, and
  renderer-handoff behavior do not regress.

## References

- `src\VideoProcessor-GUI\VideoProcessorDlg.cpp`: LLDV candidate detection,
  effective-state construction, and transition timers
- `src\VideoProcessor-Lib\blackmagic_decklink\BlackMagicDeckLinkCaptureDevice.cpp`:
  DeckLink EDID, EOTF/colorspace, and static-HDR capture
- `src\VideoProcessor-Lib\libplacebo\LibplaceboVideoRenderer.cpp`: source
  color translation, frame-state snapshot, and `OnVideoState`
- `src\VideoProcessor-Lib\microsoft_directshow\live_source_filter\ALiveSourceVideoOutputPin.cpp`:
  madVR sample side-data delivery
- [DeckLink HDR capture and Dolby Vision playback documentation](https://sdk-doc.blackmagicdesign.com/decklink-sdk/HighLevel/hdrmetadata.html)
- [DeckLink input dynamic-range enum](https://sdk-doc.blackmagicdesign.com/decklink-sdk/DeckLinkAPI/enums/bmddynamicrange.html)
- [DeckLink ancillary transport boundary](https://sdk-doc.blackmagicdesign.com/decklink-sdk/HighLevel/ancillary.html)
- [DeckLink Quad HDMI Recorder specifications](https://www.blackmagicdesign.com/au/products/decklink/techspecs/W-DLK-44)
- [Dolby Vision metadata-level semantics](https://professionalsupport.dolby.com/s/article/Dolby-Vision-Metadata-Levels)
- [libplacebo Dolby metadata contract](https://raw.githubusercontent.com/haasn/libplacebo/master/src/include/libplacebo/colorspace.h)
- [mpv release notes](https://github.com/mpv-player/mpv/releases): decoded
  Dolby metadata is forwarded to libplacebo
- [MPC Video Renderer releases](https://github.com/Aleksoid1978/VideoRenderer/releases):
  decoder-side Dolby extension processing
- VP-0014: Alpha renderer SDR BT.2020 source and target support
- VP-0018: Alpha renderer refresh-rate reselection on switch
- VP-0041: Eliminate stale-frame flashes across renderer rebuilds
- VP-0045: Namespace built-in renderer configuration as `vpvr`
