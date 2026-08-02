# VP-0053: Alpha LLDV parity and transition validation

## Status

Backlog. No implementation has started.

## User story

As an Alpha-renderer user, I want LLDV input to be interpreted and tone mapped
correctly through content, menu, HDMI-resync, refresh, renderer, and
display-profile transitions, so no HDR/PQ treatment, stale image, or queue
failure persists after the source state changes.

## Existing contract

`newlldv` classification and effective source-state construction are shared VP
logic, not renderer-specific. After a configured BT.2020 + SDR candidate has
stabilized, VP constructs an effective state containing:

- PQ transfer;
- BT.2020 primaries; and
- synthetic mastering, MaxCLL, and MaxFALL metadata, with configured LLDV
  overrides.

Alpha maps the effective state into libplacebo source color information and can
accept an EOTF/primaries-only transition in place when its selected display
rule does not change. A display-rule change may correctly require one renderer
rebuild.

This behavior is intended but has not been validated as a full Alpha LLDV
parity contract. This story validates it and fixes only evidence-backed
transition gaps. It must not make every SDR BT.2020 source become LLDV.

## Required behavior

1. With `newlldv` and both LLDV-follow modes enabled, Alpha promotes only a
   stable configured BT.2020 + SDR candidate to effective PQ.
2. Alpha receives effective PQ, BT.2020, and valid synthetic HDR metadata
   before rendering promoted LLDV content.
3. Alpha processes LLDV through its normal HDR-input tone-mapping and gamut
   pipeline; it must not render it as SDR or double tone map it.
4. Returning from LLDV to an SDR menu restores real SDR treatment promptly.
   Effective PQ, synthetic HDR metadata, a conditional display rule, and a
   stale LLDV frame must not persist.
5. SDR → LLDV, LLDV → SDR, LLDV ↔ HDR10, and refresh/HDMI-resync transitions
   converge without manual restart, reset loop, queue starvation, or dropped
   frame burst.
6. If Alpha accepts the state in place, do not restart it. If an output or
   display-rule contract changes, perform one bounded reconstruction through
   the existing coordinator.
7. Switching Alpha and the external renderer while LLDV is active gives the
   incoming renderer the current effective state, never stale raw SDR or PQ
   state.
8. NLS/shader, viewport, OSD, and display-profile hotkeys do not clear or
   reclassify an active LLDV state.
9. Genuine SDR BT.2020 not confirmed by the enabled configured heuristic
   remains SDR.

## Diagnostics

Log, once per state transition:

- raw EOTF/primaries and HDR-metadata presence;
- heuristic eligibility, stabilization, confirmation, cancellation, or
  rejection reason;
- effective state sent to Alpha, including synthetic metadata;
- Alpha's in-place versus rebuild decision and relevant display rule;
- source sequence/generation and renderer generation; and
- final steady state.

The OSD must show the effective input treatment: it must not say SDR while
Alpha is using promoted PQ LLDV, or retain HDR/PQ after the source returns to
SDR.

## Validation matrix

| Condition | Required result |
| --- | --- |
| Alpha starts on LLDV | Stable promotion and correct tone mapping without manual restart |
| SDR menu to LLDV | One promotion after stabilization, no false early promotion |
| LLDV to SDR menu | SDR restored; no stuck HDR/PQ or stale frame |
| LLDV to HDR10 and back | Correct effective state each time |
| 23.976/24 and 59.94/60 LLDV | Correct behavior across refresh switch/resync |
| Each Alpha display/LUT rule | Correct rule selection and color contract |
| Alpha ↔ external renderer while LLDV active | Incoming renderer receives current effective state |
| Shader/NLS and viewport hotkeys | LLDV state remains intact |
| Genuine SDR BT.2020 | Never promoted without configured confirmed evidence |
| Dropout/resync during LLDV | Recovery without reset loop or queue starvation |

Use NVIDIA Shield and Apple TV cases where available. Preserve relevant
`C:\Videoprocessor\vp\logs\vp_debug.log` excerpts for each failure, including source, refresh,
display rule, and renderer generation.

## Implementation guidance

- Keep classification and effective-state ownership in shared VP logic; do not
  create an Alpha-only LLDV heuristic.
- Explicitly test the Alpha accept/rebuild decision when display rules match on
  EOTF, primaries, or HDR state.
- This is input treatment. It does not require HDR output; VP captures and
  tone maps HDR/LLDV input.
- Reuse the existing reset/rebuild coordinator and stale-frame protection. Do
  not add independent timers or queue policies.
- Add focused tests for effective-state construction and Alpha's
  accept-or-rebuild decision before hardware transition validation.

## Acceptance criteria

- Alpha consumes confirmed LLDV as PQ/BT.2020 with valid synthetic HDR
  metadata and tone maps it correctly.
- All tested LLDV, HDR10, and SDR transitions reach the correct state without
  manual restart, loop, starvation, dropped-frame burst, stale frame, or stuck
  HDR treatment.
- Alpha rebuilds only when its display/output contract requires it; otherwise
  it updates in place.
- Genuine SDR BT.2020 is never misclassified as LLDV.
- Logs and OSD make raw state, effective LLDV state, Alpha's decision, and the
  final state diagnosable.
- Existing external-renderer LLDV, Alpha color/LUT, queue, refresh, and
  shader/NLS behavior do not regress.

## References

- `src\VideoProcessor-GUI\VideoProcessorDlg.cpp`: LLDV candidate detection,
  effective state construction, and transition timers
- `src\VideoProcessor-Lib\libplacebo\LibplaceboVideoRenderer.cpp`: source
  color translation and `OnVideoState`
- VP-0018: Alpha renderer refresh-rate reselection on switch
- VP-0041: Eliminate stale-frame flashes across renderer rebuilds
- VP-0045: Namespace built-in renderer configuration as `vpvr`
