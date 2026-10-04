# Magewell qualification for one VideoProcessor release

This integration ships in the same x64 Release build as the existing capture
paths. Magewell appears as a capture choice only when the official runtime can
be loaded and a usable card is discovered. A saved Magewell selection should
produce a clear unavailable message when the runtime cannot load; other capture
devices and renderers must continue working.

The **development SDK** is needed to compile VP. A viewer machine needs the
official Magewell **runtime and driver** for its card, not the SDK headers or
import libraries. The runtime is loaded from the trusted Windows system
location. Do not copy a DLL from the SDK into the VP directory or change
Defender settings to make a test pass.

## Implemented scope

This backend targets the Magewell **Pro Capture** family only. USB Capture and
Eco Capture use different capture APIs and are excluded from discovery.
Supported outputs are ARGB for RGB 8-bit, RGB10 repacked to VP's existing R210
for RGB 10-bit, and P210 repacked to VP's existing V210 for YCbCr. Output choice
is checked against the card's reported capabilities. RGB 12-bit and unknown
RGB depth are explicitly rejected; they are not silently converted to 4:2:2.
The YCbCr path delivers 10-bit 4:2:2; it does not preserve 12-bit precision or
4:4:4 chroma when those are supplied by the source.

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
`MagewellRuntimeProbe.exe --expect-no-devices`. Use `--status` to print the
actual production loader result without imposing either expectation.

The development machine used for this work has
`C:\Windows\System32\LibMWCapture.dll`. An injected missing DLL test is useful
for exercising error handling, but it is **not** a clean machine startup test.
On 2026-10-04 the production loader probe reported
`MAGEWELL_AVAILABLE: channels=0` here, as expected for a runtime-installed
machine without Magewell hardware. This observation is an interim local check;
it does not qualify a physical capture path.
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
| BT.2020 HDR | Feed PQ/HDR10 with known highlights and metadata, then HLG if available. | VP reports the intended color space and EOTF. Metadata and image update as source changes, with no retained HDR state after returning to SDR. |
| RGB 10/12-bit | Feed deep RGB, especially saturated fine detail. | Either a supported precision preserving route works and is identified, or VP clearly marks that mode unsupported. It must not silently discard chroma or precision. |
| Unsupported capture format | Use a mode the specific card cannot output, if available through source settings. | VP rejects the mode clearly and remains responsive; it does not show a black frame as a successful capture. |
| Signal transitions | Switch SDR/HDR, resolution, frame rate, RGB/YCbCr, cable state, and input connector while VP runs. Repeat several times. | Reacquisition succeeds, old metadata and frames do not linger, and no escalating memory use or deadlock appears. |
| Stop and restart | Start and stop capture repeatedly, close VP during active capture, and reopen it. | No hang, crash, use-after-free symptom, retained capture session, or failure to reopen the card. |
| Existing path | Use DeckLink or another existing capture path and each renderer normally used by the tester. | Behavior matches the beta build for the same source and settings. |

For each test, report **pass**, **fail**, or **not available** with one sentence
of observation. For a failure, include the exact mode and a log excerpt spanning
the signal change, format selection, and resulting error. Avoid editing the
active VP configuration wholesale; save a copy before any needed changes.

## Release decision

The local checks establish compilation, link isolation, controlled loader
failure behavior, and pure conversion/ownership behavior. They cannot prove
actual card discovery, connector switching, DMA buffer lifetime, timing,
colorimetry, HDR metadata, rendering of the captured frames, or recovery from
live signal changes. Those claims require the two hardware tester reports.
Keep the Magewell feature unqualified until the clean VM and hardware cases
pass. Record every unavailable signal mode so the release description states
the supported card and format scope accurately.
