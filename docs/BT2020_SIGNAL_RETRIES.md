# BT.2020 signaling retries

The VP Renderer uses NVIDIA's nonpersistent AVI InfoFrame SET command. Driver
acceptance and readback do not establish what a projector received. The new
controller repeats the selected state at approximately 0, 0.5, 1.5, 3, 5 and 8
seconds, including after a successful readback. Each new color selection replaces
the prior schedule. Display renegotiation starts a fresh schedule.

F6 with reporting enabled sends BT.2020. F5 after VP has managed that output sends
explicit Rec.709, clearing extended colorimetry. Leaving an output restores its
original colorimetry (an inherited BT.2020 value is cleared to Rec.709). Each send
reads the current AVI frame and changes only the two colorimetry fields, retaining
current timing, aspect and range fields. A failed or unverified SET retains
cleanup ownership. Successful completed output release ends that ownership.

A serialized worker performs the delayed sends even while capture is paused.
Retirement cancels pending sends before synchronous cleanup; a failed cleanup
remains owned for the existing renderer retirement recovery. No worker survives
the reporter's destruction. No persistent driver override is installed.

Targets come from the actual presentation swapchain, re-resolved on profile and
mode changes and checked once per second while rendering. There is no fallback to
the primary display. NVAPI display IDs are resolved on every attempt and checked
again before SET. If output lookup fails, the previous output is released rather
than reused as the new target.

## Validation

Native regression tests in Bt2020SignalControllerTests.cpp use a fake driver and
clock. They cover both retry directions, rapid reversal, transient failures,
unverified readback, mode changes, preservation of unrelated AVI fields, two
outputs, missing targets, bounded unsupported-output behavior, failure reporting,
and retirement ownership/cancellation.

Hardware qualification still requires the Epson 5050:

1. With both displays enabled, put VP on the Epson. Press F6 and confirm the
   projector enters BT.2020; press F5 and confirm Rec.709. Repeat at least ten
   times, including rapid F6/F5/F6 and F5/F6/F5 reversals.
2. Repeat while the existing scripts change the projector mode. Allow the final
   selection's eight-second retry window to finish. Confirm the final state on
   the projector, including disengagement.
3. Change refresh rate/fullscreen mode and reconnect the Epson while BT.2020 is
   selected. Confirm signaling targets the presentation display after recovery.
4. Switch renderer and close VP while BT.2020 is active. Confirm BT.2020 is
   cleared and no delayed VP send re-enables it after handoff.
5. Correlate projector results with `NVIDIA BT.2020 report` log entries (target,
   desired state, attempt, SET result and readback). A driver match alone is not
   physical validation. The current unsupported monitor cannot provide it.

Reference: https://docs.nvidia.com/nvapi/group__dispcontrol.html (NV_INFOFRAME_CMD).

Validation on 2026-09-28: the x64 Release solution build completed with zero
errors. All 95 selected native tests passed (13 signaling tests plus output-policy,
renderer transition and queue-launch regressions). Logs: bt2020-build-final.log
and TestResults/bt2020-regressions.trx in the worktree. Epson hardware behavior has
not yet been tested; this change has not been deployed.
