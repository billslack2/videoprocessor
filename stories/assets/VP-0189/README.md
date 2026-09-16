# VP-0189 baseline policy reproduction

These files preserve the source-review experiment from 2026-09-16. They are
baseline characterization, not fixed-behavior acceptance tests and not a replay
of the user's viewing session or star-field pixels.

- `crop_review_probe.cpp`: standalone cases calling the actual crop policy.
- `pch.h`: standard-library headers substituted for the application's PCH.
- `baseline-results.txt`: successful x64 optimized probe output against beta
  `94f938d33216d0f212797f1ed4c6f83db02d9187`.

The compiled policy's Git blob was
`054e88b06d979087cb0d0c0c4716882ec1ea79e2`, also present at `3acd02b3`.
The probe expects the baseline weaknesses. Adapt its scenarios into normal
regression tests with corrected expectations during VP-0189 implementation.
No VideoProcessor production binaries were built or deployed by this probe.

To reproduce, use a Visual Studio x64 Developer PowerShell. Set `vpSourceRoot`
to an isolated worktree containing the recorded historical policy and
`vpProbeRoot` to a temporary copy of these files. Build outside the tracker:

```powershell
$vpSourceRoot = 'E:\codex\videoprocessor\review-black-bars-20260916'
$vpProbeRoot = 'C:\Users\bslac\Documents\ChatGPT\Done\crop-review-20260916'
Push-Location $vpProbeRoot
try {
    & cl.exe /nologo /std:c++17 /EHsc /O2 /DNDEBUG /DNOMINMAX /Y- "/I$vpProbeRoot" "/I$vpSourceRoot\src\VideoProcessor-Lib" "$vpSourceRoot\src\VideoProcessor-Lib\vprenderer\AlphaSourceCropPolicy.cpp" "$vpProbeRoot\crop_review_probe.cpp" "/Fe:$vpProbeRoot\crop_review_probe.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Probe compilation failed' }
    & "$vpProbeRoot\crop_review_probe.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Baseline characterization failed' }
} finally {
    Pop-Location
}
```

The example paths are from the review machine. They are not authoritative
source checkouts or an instruction to implement from the historical commit;
future implementation must discover and fetch the current remote beta.

The checks demonstrate oscillation, immediate outward release, successful
exact-geometry recovery, permanently sticky outward evidence, and the
contained-inset recovery failure. Stable-bar and authoritative full-raster
control cases pass. They do not measure pixel-detector accuracy, end-to-end
renderer behavior, log volume, or the original report's counts/timings.
