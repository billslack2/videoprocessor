# VP-0151: Reliable explicit BT.2020 exit signaling

## Status

Backlog (2026-08-26). VP Renderer can successfully ask the NVIDIA driver to
restore an AVI InfoFrame without actually clearing BT.2020 at the projector.
Implement an explicit, bounded, observable SDR/Rec.709 exit transition rather
than treating one accepted restoration call as proof that the physical display
left BT.2020.

Confirmed source baseline: `origin/v1.3.001-beta` at
`2cfbaf2d36a8a848743714178b3fc2861be2d127`.

## User story

As a VP Renderer user switching from an SDR BT.2020 display profile to an SDR
Rec.709 profile or another renderer, I want the projector to receive a reliable
non-BT.2020 AVI InfoFrame so it cannot remain latched in BT.2020 even when VP
started while the NVIDIA driver's reported state was already stale.

## Confirmed problem

`NvidiaBt2020Reporter::Enable()` reads the current NVIDIA AVI InfoFrame into
`m_originalInfoFrame`, changes its colorimetry fields to extended colorimetry
value 6, and submits a one-shot `NV_INFOFRAME_CMD_SET`. `Restore()` later sends
the captured structure once and clears VP's active ownership as soon as NVAPI
returns `NVAPI_OK`.

This is not a reliable exit contract:

- The captured "original" value can already be BT.2020. The 2026-08-26 live log
  recorded entry with `previous_colorimetry=3 previous_extended=6`, followed by
  an exit that restored `colorimetry=3 extended=6 result=NVAPI_OK`. VP therefore
  reported a successful restore while explicitly sending BT.2020 again.
- After madVR corrected the output, a later VP entry captured `0/0`; the next VP
  exit restored `0/0` and behaved correctly. This matches the observed recovery
  when toggling BT.2020 through madVR.
- `NV_INFOFRAME_CMD_SET` is documented as a one-shot transmission. `NVAPI_OK`
  proves driver acceptance, not that a projector received or acted on the
  packet, especially around a modeset, display handoff, or link stabilization.
- `Enable()` returns early when the same display is already marked active, so
  VP has no explicit reassert path when the projector missed the first BT.2020
  packet.

## Required behavior

1. When VP changes from a profile that requested HDMI BT.2020 signaling to a
   Rec.709/manual-disabled profile, explicitly transmit a non-BT.2020/default
   AVI InfoFrame. Do not replay extended-colorimetry value 6 merely because it
   was present in the driver's entry snapshot.
2. Use the NVAPI reset/default contract or an equivalently validated payload
   that preserves unrelated AVI fields while clearing BT.2020 colorimetry.
   Document whether the chosen wire value is colorimetry `NO_DATA`, explicit
   BT.709, or driver `AUTO`, and validate it against the NVIDIA/projector path.
3. Treat InfoFrame changes as one-shot physical notifications. Provide a
   bounded, non-blocking reassert schedule after the output/display timing is
   stable so a packet lost during link or mode transition is sent again.
4. Keep transition ownership and diagnostics pending until the bounded exit
   sequence completes. A single `NVAPI_OK` must not be logged or exposed as
   receiver verification.
5. Provide a bounded force-reassert path for entering BT.2020 on the same
   display even when VP's logical state already says it is active. Normal
   repeated frames or unchanged profile evaluations must not cause per-frame
   NVAPI traffic.
6. Re-resolve the NVAPI display ID by stable display name for every retry that
   may cross a modeset, retaining the existing protection against stale display
   IDs and monitor changes.
7. Starting VP while the driver reports extended-colorimetry value 6 must not
   make that stale value the Rec.709 exit target. Startup, live profile changes,
   renderer retirement, shutdown, and renderer handoff must converge on the
   documented non-BT.2020 state.
8. Preserve the selected BT.2020 pixel transform independently from HDMI
   signaling availability. Failure to signal or clear must remain truthful in
   the OSD/log and must not silently reinterpret BT.2020-target pixels as
   Rec.709.
9. Log requested state, submitted colorimetry values, attempt number, trigger,
   NVAPI result, and final logical status without per-frame spam. Describe GET
   readback as driver evidence only, never projector verification.
10. Keep retries bounded during shutdown and handoff. This story must not
    reintroduce the renderer-retirement livelock addressed by VP-0149.

## Acceptance criteria

1. A focused reporter test starts with a captured `3/6` AVI state, enters VP
   BT.2020, exits to Rec.709, and proves that every exit submission is a
   documented non-BT.2020 value rather than `3/6`.
2. Starting with captured `0/0`, `2/0` (explicit BT.709), and driver-auto
   values preserves unrelated InfoFrame fields and produces the documented
   clear behavior without inventing BT.2020.
3. Tests simulate an accepted first exit packet that is not observed by the
   receiver-facing seam; VP performs only the configured bounded retries and
   reaches a truthful completed or unverified result.
4. Switching BT.2020 -> Rec.709 -> BT.2020 forces a fresh clear and a fresh
   BT.2020 SET. Re-selecting an unchanged profile does not create unbounded or
   per-frame NVAPI calls.
5. A modeset/display-ID change between attempts re-resolves the intended
   display and never sends the clear to a stale or different display ID.
6. Live validation on the production NVIDIA/projector path begins in BT.2020,
   switches directly to VP Rec.709, and repeatedly confirms that the projector
   exits BT.2020 without using madVR as a repair step.
7. Live validation covers cold start with the driver GET already reporting
   `3/6`, rapid profile toggles, fullscreen/window transitions, refresh-rate
   changes, renderer handoff, orderly shutdown, and a failed/unsupported NVAPI
   path.
8. Logs distinguish driver SET acceptance, optional GET readback, bounded retry
   completion, and unverified receiver state. They no longer call restoration
   successful merely because a stale `3/6` snapshot was replayed.
9. Existing BT.2020 pixel-transform, LUT, output-profile, and OSD tests remain
   valid, and a clean x64 Release build passes before deployment.

## Non-goals

- Do not infer projector state from image pixels or claim receiver-side
  verification without an actual receiver/HDMI diagnostic source.
- Do not change HDR10/PQ metadata policy, Windows HDR mode, output gamut
  transforms, LUT calibration, or DirectShow renderer signaling.
- Do not continuously transmit InfoFrames every frame.
- Do not solve unrelated display detection or `NVAPI_NOT_SUPPORTED` failures.

## Likely implementation areas

- `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`
- A focused, injectable NVIDIA InfoFrame state/retry helper and unit tests
- VP Renderer output-state diagnostics/OSD
- Renderer retirement and live-profile transition tests

## Dependencies and references

- VP-0019 introduced SDR BT.2020 output signaling.
- VP-0064 owns persisted SDR BT.2020 output and operator-visible reporting.
- VP-0134 owns safe renderer handoff and display-global state restoration.
- VP-0149 owns bounded shutdown after display-restore failure; its liveness
  guarantees must be preserved.

