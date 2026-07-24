# VP-0004: Reliable limited-range output in VP
No code changes yet. The goal is to add proper Limited RGB output without breaking the currently stable Full RGB/DWM path.
Establish a baselineRecord the current successful configuration: VP full output, Epson Video Range Full, output_gamma=AUTO.
Add diagnostic-only logging for:swapchain model and flags;
format and active DXGI color space;
CheckColorSpaceSupport and SetColorSpace1 results for Full/Studio RGB gamma 2.2 and 2.4;
adapter, driver, output, and display mode.

Validate black clipping with known patterns, not just movie scenes.

Success: a reproducible log/pattern baseline for correct Full output.
Prototype alternate presentation modes
Test these behind a temporary opt-in setting, one at a time:
Current composed/bitblt path — expected Full only.
Flip-model composed path.
Flip-model/direct presentation path.
For each, test whether Windows/NVIDIA accepts:
RGB_STUDIO_G22_NONE_P709
RGB_STUDIO_G24_NONE_P709
Full RGB equivalents
Also check whether enabling VP’s OSD changes brightness or composition behavior—the issue that led us to choose bitblt in the first place.
Success: identify at least one path that genuinely transmits Limited RGB, or conclusively prove none works on the current driver/output chain.
Implement a capability-gated output selector
Add an explicit presentation setting, for example:
output_presentation=AUTO
Possible values:
AUTO
COMPOSED
DIRECT
Behavior:
AUTO: prefer the current composed Full path unless Limited is requested and a tested Limited-capable path is available.
COMPOSED: retain the safe DWM path; reject Limited with a clear log if unsupported.
DIRECT: allow the alternate path explicitly for testing/advanced use.
output_range=LIMITED must only be considered active after VP has successfully configured the required DXGI mode. Otherwise VP should log that it is outputting Full—not silently imply that Limited worked.
Success: the OSD/log reports both requested and actual output range and presentation mode.
Fix gamma semantics along with range handlingKeep output_gamma=AUTO tied to the real negotiated swapchain color space.
Only allow explicit 2.4 if the selected presentation path actually supports a matching gamma 2.4 output mode.
If unsupported, reject the override or fall back with a prominent log entry—do not silently apply a transfer mismatch.

Success: changing output gamma cannot produce an undeclared DWM/swapchain mismatch.
Validate on the Epson
Test both matched pairs:
VP output	Epson Video Range
Full	Full
Limited	Limited

For each, check:
black clipping / near-black detail;
SDR Rec.709;
HDR-to-SDR;
OSD on/off;
fullscreen/window transitions;
F2/F3 profile changes;
refresh-rate switching;
renderer restart.
Success: the two matched configurations render equivalently in black level and grayscale, with no OSD-triggered brightness shift.
Treat SDR BT.2020 as a separate follow-up
The first work should solve range correctly for SDR Rec.709. True SDR BT.2020 output is a separate renderer feature: VP currently targets SDR Rec.709, so it would need explicit BT.2020 target primaries, DXGI signaling where available, and Epson validation with the cinema filter.
That should not be bundled into the initial Limited-range change; it would make diagnosis much harder.


9:58 PM
