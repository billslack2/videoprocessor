# VP-0004: Reliable limited-range output in VP

## Status

Done.

- Implementation branch: `v1.1.014-beta`
- Implementation worktree: `C:\Users\bslac\vp\videoprocessor - VS2026`
- Final implementation commit: `acf685d` (`v1.1.014-beta: complete VP-0004 alpha output`)
- Release x64 solution build succeeded; Release x64 unit/regression suite
  passed 49/49.
- The implementation includes a capability-gated output policy, presentation
  selector, explicit requested/actual OSD status, detailed DXGI/output logging,
  persistent libplacebo shader cache, configurable refresh-command delay,
  config-file selection, and renderer-selection shortcuts.
- Epson live validation established the locally preferred Alpha default:
  Direct + Full/sRGB (Test 02) with projector and madVR set to Full. It looked
  effectively identical for SDR and close for HDR-to-SDR, where different tone
  mappers are expected to differ slightly.
- Composed + Limited/Gamma 2.4 (Test 04) correctly reported DXGI studio-range
  rejection and safely fell back to Full/sRGB. The initially crushed blacks
  were caused by leaving the projector set to Limited after that fallback, not
  by an Alpha tone-mapping fault.
- Remaining validation note: these are locally validated defaults, not a
  general recommendation or a claim of pixel-perfect madVR equivalence. Test
  additional GPUs/drivers, display chains, capture formats, HDR content, and
  HDMI quantization with patterns/PresentMon before relying on Limited output.
The goal is to add proper Limited RGB output without breaking the currently stable Full RGB/DWM path.
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

## Readiness review and resolved design

The branch already contained a partial range implementation despite the
original status above. Review found that it could render gamma 2.4 after DXGI
fell back to G22 and treated `SetColorSpace1` success as proof of Limited
output.

The bounded API and presentation investigation established these rules:

1. libplacebo's color-space call is a hint; its returned swapchain-frame
   representation is the default rendering contract.
2. VP overrides that contract only when the exact Studio P709 color space was
   advertised before configuration and `SetColorSpace1` succeeded.
3. A failed or undeclarable request restores and renders Full/sRGB.
4. `DIRECT` means flip/direct-eligible. Windows chooses the live composed,
   DirectFlip, MPO, or independent-flip mode; PresentMon/ETW is required to
   distinguish them.
5. DXGI acceptance proves swapchain interpretation, not the HDMI wire range.
6. SDR BT.2020 remains out of scope.

Authoritative references:

- <https://raw.githubusercontent.com/haasn/libplacebo/v7.360.1/src/include/libplacebo/swapchain.h>
- <https://raw.githubusercontent.com/haasn/libplacebo/v7.360.1/src/d3d11/swapchain.c>
- <https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ne-dxgicommon-dxgi_color_space_type>
- <https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgiswapchain3-checkcolorspacesupport>
- <https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model>

## Automated acceptance

- `AUTO/AUTO/AUTO` preserves composed bitblt and Full/sRGB.
- Direct requests flip but reports the actual swap effect, including fallback.
- Limited AUTO/G24 requires pre-advertised PRESENT support, a successful Set,
  and a successful post-check. Studio G22 is diagnostic-only because
  libplacebo 7.360.1 has no exact BT.709 piecewise output-transfer enum.
- Missing SwapChain3, failed checks, or failed Set returns Full/sRGB and never
  mutates the target to Limited.
- Failed AUTO Limited negotiation recreates the stable composed/bitblt
  Full/sRGB swapchain; DIRECT remains on its explicitly requested flip path.
- Verified color-space transitions enforce Check/Set/Check ordering and do not
  call Set when pre-support is absent.
- Undeclarable Full/Auto gamma overrides cannot create a target/DXGI mismatch.
- Release and Debug x64 solution builds pass, and all Release x64
  unit/regression tests pass. Debug test discovery remains blocked by the
  existing native VLD/test-host environment issue recorded in Status.

## Epson/manual acceptance record

Record Epson model/firmware, NVIDIA driver, connection path, Windows HDR state,
resolution/refresh/bit depth, GPU output setting, VP log, and PresentMon mode.

Test matched Full/Full and Limited/Limited using a Rec.709 PLUGE/grayscale ramp
with 10-bit values around black (`0`, `63`, `64`, `65`), nominal white (`939`,
`940`, `941`), and full white (`1023`). Repeat SDR Rec.709 and HDR-to-SDR with
OSD off/on, window/fullscreen transitions, F2/F3 profile changes, refresh-rate
switching, and renderer restart.

Success requires equivalent black, near-black, grayscale, and white behavior
for both matched configurations with no OSD brightness shift. If no path
passes, retain Full fallback and record the chain as not Limited-capable.
