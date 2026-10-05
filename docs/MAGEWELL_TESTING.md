# Magewell qualification for one VideoProcessor release

This integration ships in the same x64 Release build as the existing capture
paths. Magewell appears as a capture choice only when the official runtime can
be loaded and a usable card is discovered. A saved Magewell selection should
produce a clear unavailable message when the runtime cannot load or no
supported card is present; other capture devices and renderers must continue
working.

The **development SDK** is needed to compile VP. A viewer machine needs the
official Magewell **runtime and driver** for its card, not the SDK headers or
import libraries. The runtime is loaded from the trusted Windows system
location. Do not copy a DLL from the SDK into the VP directory or change
Defender settings to make a test pass.

## Implemented scope

This backend targets the Magewell **Pro Capture** family only. USB Capture and
Eco Capture use different capture APIs and are excluded from discovery.
Supported outputs are ARGB for RGB 8-bit, RGB10 repacked to VP's existing R10l
for RGB 10-bit, and P210 repacked to VP's existing V210 for YCbCr. The default
RGB10-to-R10l path preserves 4:4:4 channel data. Output choice is checked
against the card's reported capabilities. RGB 12-bit uses a logged P210
compatibility conversion to 10-bit YCbCr 4:2:2; unknown RGB depth is rejected.
The YCbCr path
delivers 10-bit 4:2:2; it does not preserve 12-bit precision or 4:4:4 chroma
when those are supplied by the source. A forced P010 conversion reduces
4:4:4 to 4:2:0, and the existing converter has a BT.601 limitation for SD RGB.
R10l capture currently requests limited-range RGB, so full-range RGB/HDR input
needs explicit image-level qualification.

Magewell capture starts with eight reusable frame buffers and grows its pool
when renderer queue or crop lookahead holds them. The 1 GiB budget counts all
live Magewell delivery buffers for that device, including buffers still held
by downstream frame references across format changes or after StopCapture;
their bytes stop counting only when the final reference releases. A separate
SDK input scratch buffer used for P210 or RGB10 repacking is capped at 128 MiB
and is not included in that pool budget. If another delivery buffer would
exceed 1 GiB, capture reports an error and requires a restart with lower queue
depth or resolution.

Unsupported signals invalidate the video state and are polled for recovery.
Repeated SDK failures or two seconds without buffered-frame notifications
invalidate video and stop delivery with an error requiring Restart. The capture
lifecycle remains active until Stop so the existing GUI can retire its renderer
and offer Restart safely. A temporary clock-read failure returns the last valid
monotonic sample; the capture worker handles persistent clock failures.

The SDK headers are a build prerequisite for this single build. Set the
`VP_MAGEWELL_SDK_ROOT` environment variable or the MSBuild `MagewellSdkRoot`
property to the official SDK's `SDKv3` folder. No vendor import library is
linked and no vendor runtime is bundled. Keep the optional backend contained
under `src/VideoProcessor-Lib/magewell`; the GUI and configuration editor use
the small `MagewellBackend` facade. Existing DeckLink, renderer, shared frame,
and shared converter implementations are unchanged.

## Evidence to collect before hardware testing

Build the solution in **x64 Release** from the beta based feature branch.
Record the beta base commit, feature commit, SDK header version, VP
binary SHA-256 hashes, Windows version, and the Visual Studio build/test
results. Run:

```powershell
& .\tools\test_magewell_isolation.ps1
```

The isolation check requires the actual x64 Release GUI, Config, and Config
Discovery binaries. It rejects a Magewell DLL in their PE import tables and
changes outside the narrow Magewell integration surface relative to the pinned
beta. Review the complete source diff as well: the DeckLink backend,
renderers, shared frame formats, and format converters should have no changes.
Run the Magewell loader and capture unit tests through the solution test
runner. In particular, the injected loader tests must cover missing runtime,
missing exports, initialization failure, and ownership/release behavior.

For the production loader probe, build `tools/MagewellRuntimeProbe.vcxproj`
after the solution build with the same `MagewellSdkRoot` setting. It links the
fresh x64 Release `VideoProcessor-Lib.lib` and places its executable under
`tools/x64/Release`. On the clean VM, put the probe beside the packaged VP
executable so it can use the package's app-local Microsoft runtime DLLs, then
run `MagewellRuntimeProbe.exe --expect-unavailable`. If the source checkout and
built binaries are also present on that VM, pass the probe to
`test_magewell_isolation.ps1 -MissingRuntimeProbe <path>`. On a
runtime-installed machine with no card, run
`MagewellRuntimeProbe.exe --expect-no-devices`. The updated production probe
must print `MAGEWELL_NO_DEVICES` and confirm the runtime module is unloaded
before the discoverer's `Stop()` call. Use `--status` to print the actual
production loader result without imposing either expectation.

The development machine used for this work has
`C:\Windows\System32\LibMWCapture.dll`. An injected missing DLL test is useful
for exercising error handling, but it is **not** a clean machine startup test.
An earlier direct loader probe on 2026-10-04 reported
`MAGEWELL_AVAILABLE: channels=0` here. That observation predates the updated
discoverer probe and does not establish that the current no-card path releases
the runtime module; record the new `MAGEWELL_NO_DEVICES` probe result after the
final build. Neither probe qualifies a physical capture path.
Run the **same packaged VP binaries** on a clean Windows VM with neither the
Magewell runtime nor hardware. VP and Config must start, the Magewell choice
must be absent or disabled, and a saved Magewell configuration must report why
it cannot start capture. Exercise a DeckLink device there if available, and
compare it with the same build on a DeckLink machine before the SDK runtime is
installed. Repeat after the official runtime is installed but with no card:
the app must still start, report no Magewell device, and preserve existing
capture behavior. A machine whose policy blocks the vendor DLL should behave
like runtime unavailable without asking for security exclusions.

## Two external Magewell testers

Give both testers the **identical** signed or hash-recorded x64 Release package
and a short test form. Ask each for the card's exact model and firmware, driver
and runtime versions, Windows version, VP binary hash, input connector used,
source model, resolution, rate, signal format, renderer, logs from
`C:\logs\vp.log`, and whether the observed image matches the source. A screenshot
can help show image or configuration behavior, but logs and exact signal modes
are required to diagnose format decisions. Do not send the development SDK.
The existing `tools/build_installer.ps1 -PortableZip` workflow creates a
source-pinned installer and portable ZIP under `artifacts/installers`, with
SHA-256 sidecar files. For these external tests, the portable ZIP in a new,
separate folder is the least disruptive choice: each tester can use the same
archive without replacing an active VP installation or its configuration.
Package only after the final x64 Release solution build and test pass; verify
the archive hash before sharing. The signing status should be stated accurately
to testers, especially when checking Windows security behavior.

Each tester should cover all modes their source and card actually support.
Mark an unsupported mode **not available on this setup**, rather than calling
it passed. Divide rare modes across the two testers where possible.

| Case | Tester action | Expected observation |
| --- | --- | --- |
| Discovery and input selection | Connect each physical input in turn, select the corresponding VP input, and also select an unplugged or wrong connector. | Device and connector names are stable. The selected connector, not the card default, drives the signal. Wrong or unplugged input reports no signal without stale frames. |
| SDR RGB 8-bit | Feed an 8-bit RGB SDR pattern with color bars, grayscale, and edge detail. | Correct geometry, channel order, range, color, smooth motion, and no corrupt rows or black output. |
| SDR YCbCr 4:2:2 | Feed a 10-bit 4:2:2 signal, including odd/edge patterns if the source permits. | P210 to v210 conversion preserves pixel order, stride, luma range, chroma phase, and motion. |
| BT.601 SD | Feed 480i/p or 576i/p BT.601 color bars. | VP reports BT.601 and colors match the source; the capture request must not silently use BT.709. |
| BT.709 HD | Feed 720p/1080p SDR bars and grayscale. | Reported matrix, range, and pixels agree. |
| BT.2020 HDR | Feed PQ/HDR10 with known highlights and metadata, then HLG if available. If the source can produce full-range RGB HDR, test it separately. | VP reports the intended color space and EOTF. Metadata and image update as source changes, with no retained HDR state after returning to SDR. Compare full-range black and white levels; this build requests limited range for R10l, so record any clipping or level mismatch as a qualification failure. |
| RGB 10/12-bit | Feed deep RGB, especially saturated fine detail. | The default supported 10-bit route enters VP Renderer as native R10l and preserves channel order and 4:4:4 detail; 12-bit RGB uses P210/V210 compatibility conversion with reduced precision and 4:2:2 chroma. Also test any forced P010 conversion separately: it reduces 4:4:4 to 4:2:0, and the existing converter's BT.601 behavior for SD RGB needs visual qualification. |
| VP Renderer queue and crop lookahead | With a supported source, try `queue_size: 32` with `target_frames: 3`, then `target_frames: 10`; enable `active_picture_lookahead_frames: 8` on a black-bar transition. Use a copied test configuration. | Both queue targets begin presenting without a frozen startup at tested resolutions within the pool budget. Lookahead logs show available future frames when enough have arrived. If a more demanding combination exceeds 1 GiB, VP reports a capture error instead of freezing silently; reduce queue depth or resolution and restart. |
| Unsupported capture format | Use a mode the specific card cannot output, if available through source settings. | VP rejects the mode clearly and remains responsive; it does not show a black frame as a successful capture. |
| Frame delivery watchdog and recovery | After a valid signal, stop frame delivery or interrupt the input long enough to exceed the two-second no-frame watchdog. Also test brief unsupported formats and persistent SDK failures. | Brief unsupported input is polled and recovers when a supported signal returns. A prolonged delivery failure or repeated SDK error stops frame delivery, publishes invalid video state, and reports that restart is required. The capture lifecycle remains active until the existing GUI retires it; stop and restart through VP before resuming capture. |
| Signal transitions | Switch SDR/HDR, resolution, frame rate, RGB/YCbCr, cable state, and input connector while VP runs. Repeat several times. | Reacquisition succeeds, old metadata and frames do not linger, and no escalating memory use or deadlock appears. A disappearing card or failed capture reports an error and permits restart after it returns. |
| Stop and restart | Start and stop capture repeatedly, close VP during active capture, and reopen it. | No hang, crash, use-after-free symptom, retained capture session, or failure to reopen the card. |
| Existing path | Use DeckLink or another existing capture path and each renderer normally used by the tester. | Behavior matches the beta build for the same source and settings. |

For each test, report **pass**, **fail**, or **not available** with one sentence
of observation. For a failure, include the exact mode and a log excerpt spanning
the signal change, format selection, and resulting error. Avoid editing the
active VP configuration wholesale; save a copy before any needed changes.

## October 5, 2026 conversion profiling

Production source: `dafdba962487381be87fd04ccf9b07ff1375dbe5`, based on
the verified current beta `982adb0ea309a7cf314ec2c933bd3213153ff649`.
Only tests and this report changed for the investigation; capture, DeckLink,
shared converters and renderer implementation remain unchanged.

Luna (High) added and ran the tests; Sol (Medium) and Astra reviewed the
methodology and interpretation. On the local Ryzen 7 5700G, x64 Release,
the actual production repackers were measured at 3840x2160. Each case used
8 warmups and 120 measured calls, repeated in three separate test runs.
Allocation, pattern generation and correctness validation were outside the
timed region. Tests decode the output components to verify all four patterned
buffers and cover partial groups and padded rows. Timings are characterization,
not machine-dependent pass/fail thresholds.

| Operation / layout | Per-run mean range (ms/frame) | Per-run p95 range (ms) | Worst observed sample (ms) |
| --- | --- | --- | --- |
| P210 to V210, fixed scratch and four rotating outputs | 4.392–4.453 | 4.622–4.800 | 6.523 |
| P210 to V210, four rotating input/output pairs | 4.401–4.534 | 4.619–4.850 | 6.461 |
| RGB10 to R10l, four rotating input/output pairs | 2.940–3.092 | 3.156–3.589 | 3.759 |

All 35 Magewell tests passed, followed by three successful runs of both
profiling tests. Local evidence is in `artifacts/magewell-profile-20261005/`
(`magewell-all.trx`, `profile-1.trx` through `profile-3.trx`, and matching logs).
The rebuilt test DLL is under `src/VideoProcessor-Test/x64/Release/`, not the
older packaged test DLL at the repository-level `x64/Release/`.

The YUV repack consumes about 26–27% of a 59.94 Hz frame's 16.68 ms budget.
It is material overhead, but these measurements do not reproduce the tester's
roughly 30 ms delivery interval. They exclude SDK transfers, buffer pinning,
notification scheduling, the renderer callback and concurrent hardware load.
The tester has a 5700X, not this machine's 5700G; these are not interchangeable
performance measurements. Four output buffers also do not reproduce the full
observed live pool. Do not conclude that the complete capture path is fast
enough, or that the CPU contribution has been ruled out.

The adapter always requests P210 for supported YUV input, including 8-bit.
At 4K59.94 this is about 1.99 GB/s of SDK output before repack and renderer
upload. The SDK sample pins reusable transfer buffers with `MWPinVideoBuffer`;
the current adapter does not. Direct V210 or 8-bit capture and pinned buffers
are candidates for measured hardware comparisons, not proven fixes. A FOURCC
being defined in SDK headers does not establish support on the tester's card.

For the next hardware run, keep settings fixed and record 60 seconds each of
1080p59.94, 4K23.976 and 4K59.94, with input chroma/depth noted. Avoid fullscreen
toggles and profile changes during each steady interval. Collect the full log,
card/driver/firmware and PCIe negotiated link details if available. A diagnostic
build should additionally summarize SDK copy/completion time, repack time,
callback time, frame timestamp gaps and supported capture FOURCCs. Those stage
timings are not present in Alpha1; another Alpha1 log alone cannot identify
which stage is causing skipped frames. No diagnostic runtime changes or new
tester package were made by this profiling investigation.

## Release decision

The local checks establish compilation, link isolation, controlled loader
failure behavior, and pure conversion/ownership behavior. They cannot prove
actual card discovery, connector switching, DMA buffer lifetime, timing,
colorimetry, HDR metadata, rendering of the captured frames, or recovery from
live signal changes. Those claims require the two hardware tester reports.
Keep the Magewell feature unqualified until the clean VM and hardware cases
pass. Record every unavailable signal mode so the release description states
the supported card and format scope accurately.
