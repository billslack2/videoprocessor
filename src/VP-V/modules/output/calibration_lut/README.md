<!-- Copyright(C) 2026 VP-V contributors; SPDX-License-Identifier: GPL-3.0-only -->

# Automatic display calibration (`output.calibration_lut`)

Measures the display with a colour meter and writes 3D `.cube` LUTs (BT.709, P3-D65 and BT.2020 slots) that
VideoProcessor's existing calibration LUT keys load (`calibration_lut_enabled`, `calibration_lut_<slot>` in a
`[vprenderer.color.<name>]` profile). The renderer change is small: while a run is active it shows exact patch
codes instead of video. How it plugs into the renderer, how to build it and how to run it: `INTEGRATION.md` in this
folder.

## Layout

| Folder | What it holds | Built into |
|---|---|---|
| `pattern_lib/` | Which exact integer codes a patch is (8-bit, 8-in-10, 10-bit; limited / full; BTB / WTW; PQ), the patch plans, colour maths (CIEDE2000). Standard C++ only, with its own stand-alone tests (`pattern_lib/README.md`). | VideoProcessor-Lib |
| `calibration/` | The CPU core: `CalibrationSession` (one run's state machine), `DisplayModel` (fit and LUT build), `CalibrationOutputs` / `CalibrationRun` (`.cube`, `display_profile.cfg`, `index.json`, key-only Apply with backups), the meter (`Meter`, `Spotread`, `ConPtyTerminal`: ArgyllCMS `spotread` driven through a pseudo-console), `MeterCatalog`, `ArgyllInstall`, the simulated display and meter, `[calibration]` settings (`CalibrationConfig.h`). Also the phase-2 run kinds (`HdrVerify`, `AmbientCheck`, `LedCharacterize`), not used by the CLI. | VideoProcessor-Lib |
| `host/` | The renderer side: `CalibrationPatternHost` (presents the patch, reports the output facts; compiled into the VP renderer DLLs), `CalibrationPipe` (the local named pipe between a calibration session and the renderer; VideoProcessor-Lib), `CalibrationPipeSink.h` (the session's pattern sink over the pipe). | renderer DLLs / Lib |
| `tests/` | MSTest classes in VideoProcessor-Test: the meter through a real pseudo-console with a scripted fake spotread (`CalibrationMeterTests`), the pattern host policy, the pipe, and a simulated run through the pipe to `.cube` files (`CalibrationPatternHostTests.cpp`). `tests/data/spotread/` holds the fake meter's scripts. | VideoProcessor-Test |

Outside this folder: `src/VideoProcessor-Calibrate/` (the console driver), `src/VideoProcessor-FakeSpotread/` (test
tool), `src/VideoProcessor-Lib/DisplayProfile.h` (the display profile the run writes), `src/VP-V/build/*.items.props`
(the project items).

## How a run works

1. Settings: `[calibration]` (keys and defaults in `calibration/CalibrationConfig.h`; the CLI reads them from a
   separate `calibration.cfg`, see INTEGRATION.md).
2. The session reads the output format from the renderer (container bits, code bits, range, transfer, refresh)
   and builds the plan: a white drift reference, the greyscale from white down (with below-black last), primaries
   and secondaries, the cube, more drift references.
3. Each patch: exact codes (`pattern_lib/PatternCodes.h`) -> renderer (exact codes in the backbuffer, no
   libplacebo pass) -> wait until presented twice, then settle -> meter reading (near-black readings averaged,
   patches below the meter's floor marked) -> one line in `raw.jsonl`.
4. Fit (`DisplayModel`): black, a 3x3 matrix, per-channel curves and a residual field; an inverse per target
   colour; a 3D LUT of `lut_size` points; three `.cube` files.
5. Optional verify (`verify_after`): target patches through the LUT on the CPU, a dE2000 report.
6. Review shows the configuration diff. Apply (opt-in) makes byte backups (`<cfg>.before-calibration.<stamp>.bak`
   and `<run>\before\`) and changes only `calibration_lut_enabled` and the `calibration_lut_<slot>` keys of one
   Color profile, validated and saved atomically. Nothing else in the file changes.
7. Outputs: `<vp>\luts\calibration\index.json` and `<vp>\luts\calibration\<display>\<timestamp>\`
   (`raw.jsonl`, `display_profile.cfg`, `calibration_<slot>.cube`, `verify.json`, `report.html`, `before\`).

Plans (`plan`): `quick` (greyscale with BTB/WTW plus primaries and secondaries; about 10 minutes), `cube5`,
`standard` (9^3 cube; 35-45 minutes), `cube13`, `thorough` (17^3; about 2.5 hours). Times depend on the meter.

## External tools

- ArgyllCMS `spotread.exe` (a separate process, never linked; VP never ships or downloads it). Search order:
  `argyll_path`, `ARGYLL_PATH`, `PATH`, then `Argyll*` folders under the usual install roots. Argyll 3.0 or newer.
- Any meter Argyll supports; `MeterCatalog.h` only tunes sensor calibration, CCSS / CCMX and the low-light floor.
- `meter_model: simulated` = a simulated display and meter (dry run).
- The meter needs Windows 10 1809 or newer (pseudo-console).

## Tests

- Stand-alone, seconds, no solution: `pattern_lib/run_pattern_lib_tests.ps1` (see `pattern_lib/README.md`).
- After the solution build: VideoProcessor-Test classes `CalibrationMeterTests`, `CalibrationPatternHostPolicyTests`,
  `CalibrationPipeTests`, `CalibrationCliPathTests` (CPU only). `CalibrationHardwareTests` skips unless its
  `VP_TEST_*` variables are set (a real meter).
- The GPU and meter checks are manual: INTEGRATION.md, "Verification on real hardware".

## Rules for this folder

- New files carry `// Copyright(C) 2026 VP-V contributors` and `// SPDX-License-Identifier: GPL-3.0-only`.
- Calls into the existing renderer are marked `AUTOCAL-HOOK` and stay one short call each; logic lives here.
- No Windows macro names as identifiers (`small`, `near`, `far`, `min`, `max`, `interface`, `ERROR`, `IN`, `OUT`,
  `OPTIONAL`, `DELETE`); use `(std::min)` / `(std::max)`.
- Exactness: a patch is a whole code in the output format; the recorded signal is the code's own signal.
