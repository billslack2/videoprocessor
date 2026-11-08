<!-- Copyright(C) 2026 VP-V contributors; SPDX-License-Identifier: GPL-3.0-only -->

# Calibration pattern library (`output.calibration_lut/pattern_lib`)

The part of the calibration module that decides **which exact integer codes a patch is**, in the output format the
renderer reports, and which patches a run measures. It has no dependencies beyond the C++17 standard library: no
renderer, libplacebo, Windows headers, precompiled header, MSTest or project file. Anyone can compile and test it
on its own.

## Files

| File | What it holds |
|---|---|
| `CalibrationPattern.h` | POD structs that cross the renderer DLL boundary: `PatternRequest` (what to show), `PatternOutputInfo` (what the output is), `SignalTransfer`, `PatchWindowRect` (centred window of a given area share, any resolution, even offsets, at least 2 x 2). Header-only. Do not change the layout without bumping the plugin API (`VP_LIBPLACEBO_PLUGIN_API_VERSION`). |
| `ColorMath.h/.cpp` | `Vec3`, `Mat3` (multiply, inverse), RGB/XYZ/xyY/Lab, gamut primaries, CCT; `DeltaE2000` (CIEDE2000) is the one out-of-line function (`ColorMath.cpp`, moved here from `DisplayModel.cpp`). Double precision. |
| `PatternPlan.h/.cpp` | The patch sets: greyscale (ramp, near-black steps, BTB, WTW), primaries and secondaries, n^3 cubes, the verification set (ColorChecker 24, skin tones, deterministic randoms), drift references, `BuildPlan(PlanKind)`, and the time estimate. Normalised signals only (0 = black code, 1 = white code). |
| `PatternCodes.h/.cpp` | Signal to code for 8-bit, 8-in-10 (the sigma-delta's domain) and 10-bit, limited and full range; BTB/WTW legality and clipping; PQ encode/decode; settle time; the near-black reading policy (`ReadingAccumulator`); measurement order. |
| `tests/PatternLibSelfTest.cpp` | The library's own tests (a plain `main`, no framework). |
| `run_pattern_lib_tests.ps1` | Builds the tests with `cl.exe` and runs them. |

## Rules for this folder

- Include only the standard library and files in this folder. A new `#include <windows.h>`, `pch.h`, `libplacebo`
  or `IRenderer.h` here breaks the split (the self test would stop compiling, which is the point).
- `ColorMath.cpp`, `PatternCodes.cpp` and `PatternPlan.cpp` are compiled by `VideoProcessor-Lib` with the precompiled header turned
  off for these three files (`src/VP-V/build/VideoProcessor-Lib.items.props`).
- Output is exact: a patch is a whole code. The recorded measurement signal is the code's own signal
  (`PatchCodes::device`), never the requested one.

## Build and run the tests (no solution needed)

Requires any Visual Studio with the C++ x64 tools (found with `vswhere`). From the repository root:

```
powershell -NoProfile -ExecutionPolicy Bypass -File src\VP-V\modules\output\calibration_lut\pattern_lib\run_pattern_lib_tests.ps1 -OutDir <scratch folder>
```

Exit code 0 and the last line `pattern_lib self test: N checks, 0 failed` mean success. A failure prints
`FAIL <file>:<line>  <expression>`. The `vswhere ... is not recognized` line printed by `vcvars64.bat` is harmless.

The script compiles with `/std:c++17 /W4 /WX`. The equivalent by hand, in an x64 Native Tools prompt:

```
cl /nologo /std:c++17 /EHsc /W4 /WX /permissive- /Fe:PatternLibSelfTest.exe ColorMath.cpp PatternCodes.cpp PatternPlan.cpp tests\PatternLibSelfTest.cpp
PatternLibSelfTest.exe
```

The same code is also exercised through MSTest in `../tests/` (`CalibrationCliPathTests` runs a whole simulated
session), which runs with the rest of VideoProcessor-Test.
