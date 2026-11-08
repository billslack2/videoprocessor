<!-- Copyright(C) 2026 VP-V contributors; SPDX-License-Identifier: GPL-3.0-only -->

# Automatic calibration in VideoProcessor: integration guide

Audience: an experienced C++ / D3D11 / libplacebo developer who knows VideoProcessor and is new to this module.
Base: `v1.4.02-beta` (commit `a591ad3b`). Line numbers are for the branch tip that carries this file.
`LVR` = `src/VideoProcessor-Lib/vprenderer/LibplaceboVideoRenderer.cpp`, `LVRH` = its header.

## 1. What it does

A calibration run measures the display with a colour meter and writes 3D `.cube` LUTs that VP already knows how
to load (`calibration_lut_enabled`, `calibration_lut_bt709` / `_p3_d65` / `_bt2020` in a
`[vprenderer.color.<name>]` profile; `LoadDisplayLut` -> `ConfigureDisplayLutForTarget` -> libplacebo's target LUT).
Nothing in VP's LUT path changes. What is new:

- the renderer can show **exact patch codes** instead of video, on request, through a small contract
  (`GetOutput` / `SetPattern`, section 3);
- a **local named pipe** in the renderer DLL serves that contract to another process, **only** when
  `VP_CALIBRATION_PIPE=1` is in VideoProcessor.exe's environment (off: nothing listens, no behaviour change);
- a **console driver**, `VideoProcessor-Calibrate.exe`, runs one calibration session (the module's own
  `CalibrationSession`), drives the patches over the pipe, reads ArgyllCMS `spotread` (or a simulated meter) and
  writes the `.cube` files; with `--apply` it also sets the LUT keys in one Color profile (after a backup).

The module's own overview (layout, run steps, plans, outputs): `README.md` in this folder.

## 2. What was added to the tree, and why

New files (all GPL-3.0-only, `VP-V contributors`):

| Path | Why |
|---|---|
| `src/VP-V/modules/output/calibration_lut/pattern_lib/` | exact patch codes, plans, colour maths; stand-alone tests |
| `src/VP-V/modules/output/calibration_lut/calibration/` | the CPU core (session, fit, LUT and `.cube` output, spotread meter over ConPTY, simulation) |
| `src/VP-V/modules/output/calibration_lut/host/CalibrationPatternHost.h/.cpp` | renderer side: presents the patch, reports output facts, owns the pipe server (compiled into both renderer DLLs) |
| `src/VP-V/modules/output/calibration_lut/host/CalibrationPatternPolicy.h` | the host's pure decisions (container / code bits, max code, try-lock policy), CPU-tested |
| `src/VP-V/modules/output/calibration_lut/host/CalibrationPipe.h/.cpp` | the pipe: wire format, server (renderer), client (CLI), abandoned-patterns timer (VideoProcessor-Lib) |
| `src/VP-V/modules/output/calibration_lut/host/CalibrationPipeSink.h` | the session's `IPatternSink` over the pipe (CLI and tests) |
| `src/VP-V/modules/output/calibration_lut/tests/` | `CalibrationMeterTests.cpp` (+ `data/spotread/`), `CalibrationPatternHostTests.cpp`, `CalibrationTestSupport.h` |
| `src/VideoProcessor-Calibrate/` | the console driver (`CalibrateMain.cpp`, project) |
| `src/VideoProcessor-FakeSpotread/` | a scripted stand-in for spotread used by the meter tests (built to `x64\Release\test-tools\`) |
| `src/VideoProcessor-Lib/DisplayProfile.h` | the display profile record a run writes (`display_profile.cfg`) |
| `src/VP-V/build/*.items.props` | the project items, one props file per consuming project |

Existing files touched (every change marked `AUTOCAL-HOOK`; nothing reformatted):

| File | Change |
|---|---|
| `Directory.Build.targets` | `src` added to the include path so `<VP-V/...>` includes resolve |
| `VideoProcessor.sln` | two projects: `VideoProcessor-FakeSpotread`, `VideoProcessor-Calibrate` (Release x64) |
| `src/VideoProcessor-Lib/VideoProcessor-Lib.vcxproj` | one `<Import>` of `src/VP-V/build/VideoProcessor-Lib.items.props` |
| `src/VideoProcessor-Test/VideoProcessor-Test.vcxproj.vcxproj` | one `<Import>` of its items props |
| `src/VideoProcessor-VPRenderer/VideoProcessor-VPRenderer.vcxproj`, `...VPRendererBeta.vcxproj` | one `<Import>` each (the pattern host) |
| `LVRH` (362 lines at base; +5) | LVRH 33: a forward declaration; LVRH 300-303: `m_calibrationPatternActive`; two private methods |
| `LVR` (26524 lines at base; +88 -2) | the include and the call sites below |

### Renderer call sites (LVR)

| # | Where | What and why |
|---|---|---|
| S0 | LVR 6: top, after `#include "LibplaceboVideoRenderer.h"` | `#include <VP-V/.../host/CalibrationPatternHost.h>` |
| S1 | LVR 5595: `Impl::RetireOutput`, just before `pl_swapchain_destroy(&swapchain)` | `calibrationHost.Retire(gpu)`: stops the pipe (joins its thread), clears the patterns flag, frees the patch texture before the GPU goes. RetireOutput runs on every path that ends an Impl (Retire, `~Impl`; FinalizeRetirementForShutdown requires it), so no pipe request can run against a dying Impl. |
| S2 | LVR 5651-5667: `Impl`, after `externalStateAbandonedForShutdown` | the `CalibrationPatternHost::Adapter calibrationHost` member and `PresentCalibrationPattern()`: one refresh of the patch on whichever output path is live (VP's own DXGI flip swapchain `vpOwnedSwapchain`, or libplacebo's `pl_swapchain`), with the renderer's existing `ReadR10Texture` as the readback for `PatternReadback`. |
| S3 | LVR 21253: `OnVideoFrame`, after the RENDERING state check | `if (IsTakingOver(...)) return;`: frames are dropped, not queued, while patches show (a run lasts minutes). Atomic read only. |
| S4 | LVR 24912-24919: `RenderLoop` init, after `m_implInitialized.store(true)` | `StartPipeIfEnabled(...)`: starts the pipe server once per Impl when `VP_CALIBRATION_PIPE=1`; its handlers capture this Impl. |
| S5 | LVR 24968-24975: `RenderLoop`, first statement of the `for (;;)` body | `TakeOverPresentation(...)`: while patches show, check the stop flag under `m_queueMutex`, present under `renderMutex`, `continue`. It sits before the repeat-cadence block so that path never presents a cached video frame between patches. Present is paced by `Present(1, 0)` / `pl_swapchain_swap_buffers`. |
| S6 | LVR 25163-25166: `RenderLoop`, the preview wait (`m_queueChanged.wait(previewLock, ...)`) | predicate `|| IsTakingOver(...)`, then `continue`: once frames are dropped the queue stays empty, so the loop must wake to reach S5. |
| S7 | LVR 25343-25348: `RenderLoop`, the dequeue wait (`m_queueChanged.wait(lock, ...)`) | same as S6. |
| S8 | LVR 21839-21881: after `RequestCalibrationLutReload` | `GetCalibrationOutput(Impl&, ...)` and `SetCalibrationPattern(Impl&, ...)`: the contract (section 3). |

The other `m_queueChanged` waits (`ConversionLoop`, `WaitForDisplaySlot`'s `wait_for`) are not on the path while
patches show and are unchanged.

## 3. The renderer contract (the IVpvCalibration equivalent)

The session never touches the GPU. It needs two calls; here they are `LibplaceboVideoRenderer` private methods
served on the pipe thread. `IVideoRenderer` and the plugin API are **not** changed by this integration; if a
renderer-interface form is wanted later, the same two calls can become a small extension interface.

```cpp
// pattern_lib/CalibrationPattern.h (POD)
struct PatternOutputInfo { bool available; uint32_t containerBits /*8|10; 16 = float, refused*/, codeBits /*8 in a
  10-bit container = 8-in-10*/; bool limitedRange; SignalTransfer transfer; bool bt2020; uint32_t width, height;
  double refreshHz /*0 = unknown*/; uint64_t presentedGeneration, presentsOfGeneration; uint64_t readbackGeneration;
  uint16_t readbackCenter[3], readbackCorner[3]; char reason[256]; /* HDR-verify fields, left empty here */ };
struct PatternRequest { bool active /*false = back to video*/; uint64_t generation /*new for every patch*/;
  uint16_t patch[3], background[3] /*CONTAINER codes*/; float windowAreaPercent /*100 = full field*/; uint32_t flags; };

bool GetCalibrationOutput(Impl&, PatternOutputInfo& info) const;  // false: info.reason says why
bool SetCalibrationPattern(Impl&, const PatternRequest&, std::string& reason);
```

Semantics the renderer keeps (the fidelity contract):
- A patch is written as raw integer codes into the swapchain backbuffer in its own format (`pl_tex_clear` of the
  wrapped backbuffer; a windowed patch is a cleared texture of the same format blitted 1:1 with nearest sampling).
  No libplacebo render pass runs for it: no tone / gamut mapping, target LUT, deband, dither or user shader.
  The swapchain format, range, DXGI colour space, HDR metadata and refresh stay as configured for video.
  BTB / WTW codes reach the display unchanged. A float swapchain is refused (`PatchMaxCode` 0).
- While a patch is active incoming frames are dropped and the loop presents the patch once per refresh;
  `presentedGeneration` / `presentsOfGeneration` tell the session when the new patch is on screen (it waits for
  two presents, then `settle_ms`).
- `active == false` returns to video (a new queue generation; idempotent).
- If the surface cannot take a blit the windowed patch is shown full field (exact codes still) and the log says so
  once per run (`Calibration patterns: windowed patch unavailable ...`).

Threads and locks:
- Both calls run on the pipe server's thread. `SetCalibrationPattern` stores the request under the adapter's
  mutex, flips the renderer's `std::atomic_bool m_calibrationPatternActive`, calls `BeginQueueGeneration` and
  notifies `m_queueChanged` (never under `renderMutex`).
- `GetCalibrationOutput` try-locks `renderMutex` (up to 100 x 2 ms; while patches show and the format is cached,
  one try) to read the format facts, else answers from a cache; the generation counters are atomics.
- The capture thread (S3) and the waits (S6, S7) only read the atomic. `queueMutex` is never taken inside
  `renderMutex` by this code.

Output facts (`OutputFacts`, filled in S8 under `renderMutex`): `negotiatedSwapchainFormat`,
`activeSettings.displayBitDepth`, range from `EncodingLevels(actualOutput.encoding)`, transfer from
`EncodingTransfer(actualOutput.encoding)`, BT.2020 from `TargetIsBt2020()` (or a PQ / HLG transfer),
`renderLoadDisplayRefreshHz` (0 = unknown; the session does not need it), available = swapchain present and not
retired.

### No generic output-facts view

A generic renderer view of these output facts (a `GetOutputFacts()` call) is **not needed here**: the concrete
contract is the `gather` lambda in `GetCalibrationOutput` (S8) returning `CalibrationPatternHost::OutputFacts`,
read with `renderMutex` held. A generic view is only worth adding when a second consumer needs the same facts.

## 4. The pipe

`\\.\pipe\VideoProcessor-Calibration-<session id>`, started by S4 only with `VP_CALIBRATION_PIPE=1`.

Access, since the pipe can put patterns on screen:
- local clients only (`PIPE_REJECT_REMOTE_CLIENTS`);
- an explicit protected DACL `D:P(A;;GA;;;<user SID>)(A;;GA;;;SY)`: the user VideoProcessor runs as and SYSTEM
  only; no administrators, other users or inherited entries (`PipeSecuritySddl`);
- a per-session name (the Windows session id of the process), so other logon sessions do not find it; the CLI
  computes the same name in its own session;
- one instance created with `FILE_FLAG_FIRST_PIPE_INSTANCE`, so nobody can pre-create the name to intercept it;
  a second renderer that cannot create it logs `Calibration pipe: not started: ...`;
- an elevated VideoProcessor adds the mandatory label `S:(ML;;NW;;;HI)` (high integrity, no write up), so a
  non-elevated process of the same user cannot drive it;
- the client connects with `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION` (the server cannot impersonate it),
  checks the server with `GetNamedPipeServerProcessId` and that process's token user SID, and refuses a server that
  does not run as its own user ("refusing the calibration pipe: ...") before sending anything.
- Run the CLI as the same Windows user as VideoProcessor, in the same session and with the same elevation. A
  non-elevated CLI against an elevated VideoProcessor prints "VideoProcessor is running as administrator: run this
  tool as administrator too" (`ConnectErrorText`); a failed server check prints "refusing the calibration pipe: ...".
- One client per session: a second `VideoProcessor-Calibrate` started while one runs exits with "another
  VideoProcessor-Calibrate is already running in this Windows session" (exit code 4; a `Local\` named mutex), so two
  sessions never interleave patches.

Wire format: one request per connection (`TransactNamedPipe` after the server check), message mode. Header: magic `VPCL`, version 1, op,
payload size; ops 1 GetOutput, 2 SetPattern. Every message is capped at 4096 bytes (a larger one is refused unread
with an error reply). The two POD structs cross as raw bytes with their `sizeof` (the CLI and VP must come from the
same build) and every field is validated before use (`ValidatePatternRequestBytes`, `ValidateOutputInfoBytes`):
bool bytes 0 / 1, the transfer enum range, container / code bits, codes at most 1023, a finite window share in
(0, 100], known flags only, the tone-curve count, terminated strings; reason strings are bounded to 255 bytes. No
allocation follows a length read off the pipe. The host additionally refuses an active patch whose codes do not fit
the current container (8-bit: 255; 10-bit: 1023) or before the output is known (`PatchFitsContainer`).

Safety: if patches are on and no request arrives for 30 s (the CLI crashed or was killed), the server ends the
patches itself and video returns (`Calibration pipe: patterns ended by the renderer (...)`). The CLI sends a
GetOutput heartbeat every 5 s, so long meter readings do not trip it. Ctrl+C in the CLI cancels the session,
which ends the patches.

## 5. Build

From a Visual Studio Developer prompt in the repository root (Qt and the MWCapture SDK as the solution already
requires):

```
MSBuild.exe VideoProcessor.sln /t:Build /m /p:Configuration=Release /p:Platform=x64 /p:MagewellSdkRoot=<MWCaptureSDK>\SDKv3 /v:minimal
```

Outputs: `x64\Release\VideoProcessor-Calibrate.exe`, the two renderer DLLs (with the host), `x64\Release\VideoProcessor-Test.dll`,
`x64\Release\test-tools\fake_spotread.exe`. `VideoProcessor-Calibrate` uses toolset v142 and whole-program
optimisation like `VideoProcessor-Lib`, whose objects it links. If the linker reports LNK1103 / LNK1318, delete the
stale `*.iobj` / `*.ipdb` under `x64\Release` and the projects' `x64\Release` folders and build again.

Tests (CPU only):
```
powershell -NoProfile -ExecutionPolicy Bypass -File src\VP-V\modules\output\calibration_lut\pattern_lib\run_pattern_lib_tests.ps1 -OutDir <scratch>
vstest.console.exe x64\Release\VideoProcessor-Test.dll /Platform:x64 /Logger:"trx;LogFileName=cal.trx" /ResultsDirectory:<scratch> /TestCaseFilter:"FullyQualifiedName~CalibrationMeterTests|FullyQualifiedName~CalibrationPatternHostPolicyTests|FullyQualifiedName~CalibrationPipeTests|FullyQualifiedName~CalibrationCliPathTests"
```
vstest prints no summary for this DLL: read the TRX `Counters` line. Expected: 27 tests, all passed. Adding
`|FullyQualifiedName~CalibrationHardwareTests` gives 27 + 1 hardware stub (`RealMeterReadsThroughSpotread`, which
passes as a skip unless its `VP_TEST_*` variables and a meter are present). `CalibrationMeterTests` needs Windows 10
1809+ and `fake_spotread.exe`.

### Cross-process test without capture hardware

VideoProcessor.exe creates its renderer (and so the pipe) only when a DeckLink or Magewell device delivers a
signal. Without one, `src/VideoProcessor-CalibrationHost` (a test tool; not in the solution, never packaged) hosts
the real renderer DLL the way `LibplaceboPluginProxyTests` does: a hidden 320x180 popup window (never shown), a
synthetic SDR BGRA state, `VP_CALIBRATION_PIPE=1` in its own environment. Build it after the solution:

```
MSBuild.exe src\VideoProcessor-CalibrationHost\VideoProcessor-CalibrationHost.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=<repo>\ /p:MagewellSdkRoot=<MWCaptureSDK>\SDKv3
```

Run it from a scratch folder holding a copy of the runtime (`VideoProcessor-CalibrationHost.exe`,
`VideoProcessor-Calibrate.exe`, `vprenderer\`), because VP resolves `VideoProcessor.cfg`, `logs\` and caches from the
executable's folder and up to two parents. Put a renderer config there with `switch_refresh_rate: never`, then:

```
VideoProcessor-CalibrationHost.exe --config <scratch>\VideoProcessorRenderer.cfg --seconds 300
VideoProcessor-Calibrate.exe --config <scratch>\VideoProcessor.cfg --yes --skip-warmup     (second console)
```

with a `calibration.cfg` such as `meter_model: simulated`, `plan: quick`, `lut_size: 17`, `patch_window_percent: 1`,
`settle_ms: 40`, `warmup_minutes: 0`, `verify_after: false`. The host prints `host: rendering pid=... pipe=...` and
the renderer's `Calibration ...` log lines. This proves the cross-process path (pipe, server check, output facts,
patch take-over, return to video); it does not show the patches on a display.

## 6. Run the CLI driver

1. CPU dry run, no VP, no GPU, no meter (a minute):
   `x64\Release\VideoProcessor-Calibrate.exe --simulate --plan quick --lut-size 17 --yes --skip-warmup --config <scratch>\VideoProcessor.cfg`
   Expect `[Review]`, then `[Discarded]`, the run folder `<scratch>\luts\calibration\simulated\<timestamp>\` and the
   `.cube` paths.
2. Settings for real runs: a `calibration.cfg` next to VideoProcessor.cfg (or `--settings <file>`), e.g.
   ```
   [calibration]
   meter_model: auto
   display_kind: projector
   plan: quick
   lut_size: 33
   warmup_minutes: 30
   verify_after: true
   ```
   (all keys and their ranges: `calibration/CalibrationConfig.h`; `[calibration]` is kept out of VideoProcessor.cfg
   because the main configuration schema does not own that section).
3. Start VP with the pipe enabled, from its deployment folder:
   `cmd /c "set VP_CALIBRATION_PIPE=1 && VideoProcessor.exe"`, select the VP Renderer, play any source. The log
   shows `Calibration pipe: listening (VP_CALIBRATION_PIPE=1)`.
4. `VideoProcessor-Calibrate.exe --config C:\path\to\VideoProcessor.cfg` (add `--meter simulated` for a dry run
   through the real renderer, `--apply --color-section vprenderer.color.Rec709` to apply at Review). The tool
   prints the output format it found, then the steps; at meter calibration and placement it waits for Enter
   (`s` skip, `c` cancel; `--yes` answers Continue).
5. Without `--apply` the run is kept, not applied: set `calibration_lut_enabled: true` and
   `calibration_lut_bt709: <printed value>` (or the slot your profile's `sdr_target_primaries` selects) and press
   Apply in Config; with `--apply` press Apply in Config (or restart VP) so the renderer reloads the LUT.

## 7. Verification on real hardware (for the first GPU + meter tester)

Not run before hand-over: everything below needs a GPU, a display and (from step 3) a meter.

1. **Pipe and take-over.** VP with `VP_CALIBRATION_PIPE=1`, VP Renderer, a source playing.
   `VideoProcessor-Calibrate.exe --meter simulated --plan quick --lut-size 17 --yes --skip-warmup`.
   Expect: the console reports the real swapchain format (10-bit container on a 10-bit output; 8-in-10 when
   `display_bit_depth: 8`); patches replace video (greys, then colours), more than 50 patches measured, `[Review]`,
   `[Discarded]`, video returns. VP's log: `Calibration patterns: started (video paused)` / `ended (video resumes)`,
   no `present failed`. Repeat with the Beta renderer and with VP's own DXGI presenter
   (`diagnostic_vp_owned_dxgi_presenter`) if you use it.
2. **Exact codes in the backbuffer.** On a 10-bit swapchain, send three requests with `flags = PatternReadback`
   (a small test client: `CalibrationPipe::Client::SetPattern`, then poll `GetOutput` until
   `presentsOfGeneration >= 2`): full white (limited range: 940,940,940), a 10 % window of 502,64,4 on a 64,64,64
   background, and a below-black 32,32,32 full field. Each must report `readbackGeneration == generation` and
   `readbackCenter` = the patch codes, `readbackCorner` = the background (full field: the patch). The VP log prints
   the same (`Calibration patterns: read back generation ...`). This proves no libplacebo pass touched the patch.
   With an HDMI analyser, also check that the codes arrive unchanged at the link (BTB 32 and WTW 1000 included).
3. **Meter session.** ArgyllCMS installed (`spotread -?` works), meter attached, `meter_model: auto` (or the model
   id), `display_type` as Argyll lists it for the display, `warmup_minutes` as the display needs.
   `VideoProcessor-Calibrate.exe --plan quick` then `--plan standard`. Watch: meter calibration prompt (i1Pro),
   placement prompt, near-black reads, drift references, `[Review]` with the fit summary and dE report.
4. **Apply the LUT.** Either `--apply --color-section <your Color profile>` or by hand (section 6, step 5).
   Check the backup `<cfg>.before-calibration.<stamp>.bak`, that only the calibration keys changed, and that Ctrl+I
   shows the LUT active after Apply in Config.
5. **Re-measure.** With the LUT active, measure a verification set through video (e.g. your usual patterns through a
   source, or ArgyllCMS with VP showing patterns through the normal video path) and compare with the run's
   `verify.json` prediction. Patches from the CLI bypass the LUT by design, so a second calibration run is **not** a
   re-measurement of the LUT.
6. **Failure paths.** Kill the CLI during a run: within about 30 s VP returns to video
   (`Calibration pipe: patterns ended by the renderer`). Stop the source during a run: the session fails with the
   renderer's reason and VP recovers. Start the CLI without the variable: it says how to enable the pipe. Start a
   second CLI during a run: it exits at once with the "already running" message; the first run is unaffected.
7. **Elevation matrix** (needs UAC prompts, so it is an owner / tester check; expected results):

   | VideoProcessor | CLI | Expected |
   |---|---|---|
   | not elevated | not elevated | connects; runs |
   | elevated | elevated | connects; runs |
   | elevated | not elevated | refused at once: "VideoProcessor is running as administrator: run this tool as administrator too" |
   | not elevated | elevated | connects (same user SID; the elevated client may open the medium pipe); runs |
   | another user | any | "the calibration pipe is not there" (another session) or a refusal naming the other user |

## HDR content tone-mapped to SDR

From the renderer's code at `a591ad3b` (no change in this integration):
- `ConfigureDisplayLutForTarget` (LVR 8319 at base; called at 15426 for every rendered frame) sets
  `target.lut = displayLut; target.lut_type = PL_LUT_NORMALIZED`: the LUT applies to the gamma-encoded target RGB
  **after** tone mapping (DTM) and gamut mapping and **before** range encoding.
- `LoadDisplayLut` (LVR 8180 at base) picks the slot (`calibration_lut_bt709` / `_p3_d65` / `_bt2020`) from the
  profile's `sdr_target_primaries`, not from the content.
- So HDR content tone-mapped to SDR goes through **the same LUT slot as SDR content**: one calibration covers both.

Calibrate in the SDR output mode you watch in: the same Color profile with the same `sdr_target_primaries`,
`hdr_tone_map_target_gamma` and `sdr_lut_input_gamma`. A LUT measured under one of these settings and used under
another is not valid.

HDR passthrough output (no tone mapping, a PQ signal to the display) is **not covered**. Checked in the code at
`a591ad3b`: this renderer does not offer that output mode. `LibplaceboOutput::DxgiEncoding` has only SDR encodings
(gamma 2.2 / 2.4, BT.709 or BT.2020 primaries, full or limited), there is no PQ (G2084) swapchain colour space in
LVR, and the header states that the backend "deliberately does not emit HDR". If a later renderer adds HDR output,
it needs its own calibration (and the HDR verify, which needs the renderer's tone curve).

## 8. Known limits

- Untested on a GPU, display or meter in this tree; only the CPU tests ran (section 5). The patch-presentation
  logic was exercised on a GPU in an earlier renderer before it was adapted for this tree; that does not replace
  the checks in section 7.
- SDR LUT run only: the HDR (PQ) verify needs the renderer's tone curve (not reported here; `toneCurveCount` 0).
  This renderer outputs SDR encodings only, so the transfer is reported as SDR.
- The LUT is the renderer's existing libplacebo target LUT (applied inside libplacebo's colour pipeline).
- The pipe structs are raw POD: the CLI and VideoProcessor must come from the same build.
- No GUI: the Config app has no calibration page here; the CLI is the host. `[calibration]` lives in a separate file.
- The Dynamic Drive (LED projector) and ambient-light run kinds in `calibration/` are compiled but not reachable
  from the CLI.
- One pipe server per Windows session (the name carries the session id); a second VP instance in the same
  session does not serve calibration.

## 9. Licence

New files: GPL-3.0-only, `Copyright(C) 2026 VP-V contributors` (header in each file). The repository's own terms
(LICENSE.txt, README "License & legal") apply to the combined work; existing files keep their headers.
