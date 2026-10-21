# Subtitle bar scan performance

The render-thread Classic bar-content scan previously called the full luma/chroma
sampler for every brightness-only sample. MeasureLumaRowContent validates each
row once and reads only the same upper ten luma bits for P010, P210 and decoded DV.
Other formats keep the canonical sampler. Sample coordinates, black threshold,
minimum rows/span and content peak counts are unchanged.

The frame-batch measurement cache also reuses this raw scan for repeated outputs.
Its key includes source identity, retained backing/layout, policy/continuity,
exact picture bounds and measured black floor. It never caches presentation
confirmation, hold/drift clocks, AR admission or final output composition.

## Reproduce

Build the x64 Release library. In a VS 2019 x64 Developer command prompt (v142,
matching the library), from the checkout root:

```
cl /nologo /std:c++17 /EHsc /O2 /MD /I src\VideoProcessor-Lib tools\benchmark_subtitle_bar_scan.cpp x64\Release\VideoProcessor-Lib.lib /Fe:artifacts\subtitle-bar-scan.exe /link /LTCG
artifacts\subtitle-bar-scan.exe
```

Create artifacts first if absent. Optional arguments are local native capture
paths with existing `.txt` sidecars. Do not commit captured pixels. Captures with
no recorded picture bounds use a synthetic one-eighth bar boundary for comparison;
that does not assert the actual source AR.

The benchmark compares exact results against the original pixel loop, checks row
threshold/count/span against Sample(), validates same-frame reuse and context
invalidation, and reports uncached and cached scan costs separately.

## Local result, 2026-10-20

Using the actual v142 x64 Release library, the captured continuous-subtitle DV
frame took 5.08 ms with the original scan and 0.35 ms with the row scan (14.7x).
Synthetic UHD P010/P210/decoded-DV scans improved 13.9-16.2x. Packed/RGB captures
use the unchanged sampler and showed approximately unchanged first-scan time;
repeated outputs still reuse their same-frame result.

These numbers cover this CPU scan, not full rendering. Live subtitle transitions,
60 Hz native input and 75 Hz smooth output still require playback verification.
No detection frames are skipped, and no AR or subtitle presentation policy is
changed by this optimization.
