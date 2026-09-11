# VP-0179: trace HDR/BT.2020 return to Rec.709

This build adds diagnostics to beta a33c5bf3. Recovery behavior, the default
bounded-invalid policy, source overrides, and active configuration are unchanged.
It is not a second claimed fix for the reported regression.

## Capture a useful session

1. Use the existing configuration and madVR settings with this diagnostic build.
   The new trace works with normal logging; enhanced logging is not required.
2. Start on a known Rec.709 menu, play the same HDR/BT.2020 content, then return
   to that menu. Leave the failed state alone for at least 35 seconds to capture
   VP snapshots and the existing 30-second madVR runtime sample.
3. If still stuck, invoke Restart Renderer and wait 35 seconds. If that does
   not repair it, invoke Restart Capture and wait another 35 seconds. The log
   distinguishes the two command paths. Record whether each action repaired it.
4. Preserve logs/vp.log and its numbered rotations from beside the test
   executable. Record whether the stuck indication is VP input, madVR input,
   projector/receiver information, or visibly wrong colors. If the application
   is restarted, the preceding session is in vp.log.0 and older rotations.

A madVR matrix of TV.709 is input evidence; it does not by itself establish
HDMI output primaries. Keep the receiver/projector observation with the log.

## Read the boundaries

All new records start with `Color pipeline:`. Correlate the capture `run`,
notification `sequence`, and renderer `generation`; never compare generations
as if they were one live graph.

| Stage | Evidence |
| --- | --- |
| decklink | Existing GetInt results, values actually returned, cached values, frame flags, input/HDR presence, and counts of missing interfaces or failed reads. First sample per run, up to one change report per second, and a 10-second steady-state heartbeat. `window_changes` retains evidence of transitions coalesced by throttling; the logged snapshot is the latest sample, not every intermediate value. |
| capture-publish | Raw valid/EOTF/colorspace/HDR presence and the sequence posted to the UI. HDR float-only updates do not add normal-level trace traffic. Existing superseded/stale notification logs explain discarded messages. |
| capture-applied-* | Raw versus effective state after overrides/profile processing, graph admission result, restart intents, capture/renderer state, EOTF candidate, invalid grace and reset deferral. |
| periodic-10s | Same UI state and required/acknowledged/applied sequences even when no further state event arrives. |
| renderer-stop-request / renderer-running | Pipeline state at graph retirement request and accepted replacement. Existing retirement/reset logs give completion and failure reasons. |
| media-type-offered / media-type-connected | Requested and actual connected madVR input media type, primaries/matrix/transfer/range, color-info flag, and explicit DirectShow overrides. Queries run once on the graph owner during connection; a diagnostic-query failure does not fail startup. |
| hdr-update | Identifies the existing path that skips a null HDR update and retains a prior block. This build does not change that behavior. |
| restart-renderer-command / restart-capture-command | Marks which recovery command was invoked and the pipeline state immediately before it. |

Existing `madVR runtime info` records now include the renderer generation.
They retain the reported `yuv_matrix`, `hdr_output` and known/unknown flags,
without additional polling or interpreting missing values as SDR.

DeckLink raw EOTF 0 is SDR and 2 is PQ. Colorspace values are SDK FourCC values:
Rec.709 = 0x72373039 and BT.2020 = 0x32303230 (printed as signed decimal in
`*_colorspace` fields). A read value -1 with a failed HRESULT or a missing
metadata interface is unavailable evidence, not a Rec.709 result.

## What the trace should distinguish

- DeckLink never returns Rec.709, or a read fails and a cached BT.2020 value
  survives: capture/source/driver boundary.
- DeckLink and raw publication return Rec.709, but the applied sequence does
  not catch up: notification handling/ingress boundary.
- Raw is Rec.709 but effective state remains BT.2020: input overrides/profile
  boundary; the UI trace records the resulting contract.
- Effective state is Rec.709 but no replacement generation appears: inspect
  EOTF candidate, reset-active flag and retirement/stop logs.
- A replacement connects with BT.2020: inspect offered versus connected
  fields and explicit DirectShow overrides.
- A replacement connects as Rec.709 but madVR or the display remains BT.2020:
  downstream renderer/signaling boundary; retain the actual display evidence.

## Historical result of this pass

Comparison of 785e5911 with the pre-VP-0170 parent of 93eb9370 shows no change
to the DeckLink EOTF/colorspace GetInt success-only cache behavior. The older
periodic full-capture fallback remains present before VP-0170. The newer
metadata-only classification still treats EOTF and colorspace changes as
material. The formatter-range changes do not replace the DirectShow matrix
or primaries mapping. Earlier graph/ingress lifecycle changes remain possible
contributors, but source inspection alone has not established the failing
boundary. VP-0170 is a later repair attempt, not the original regression.

Validation scope: native trace-policy tests cover rate limiting, a brief change
that reverts, steady-state heartbeat, read failures and capture-run isolation.
The release build checks host integration. Hardware reproduction is still
needed to identify the reported root cause.
