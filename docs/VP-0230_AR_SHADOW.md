# VP-0230: first independent AR shadow experiment

Historical first-stage guide. The next active trial is described in [VP-0230_AR_ACTIVE.md](VP-0230_AR_ACTIVE.md).

Base: local `43a443cc6d4a4892bcffdee989311b1fdb624de8`, as requested, retaining the other agent's subtitle/presentation work. This is diagnostic code, not an enabled replacement detector or a fix for the known presentation-authority incidents.

## Scope

`ExperimentalAspectDetector.h` measures original-resolution pixels. Nine strips per edge use a three-pixel median parallel to the boundary, robust bar noise and darkness, local contrast, agreement between strips and paired-edge symmetry. The selected boundary is refined in source pixels. A familiar ratio contributes only to the reported score: it never selects, rounds or forces geometry. Scores are provisional rankings, not calibrated probabilities.

`Unknown`, measured bars and positive full-extent evidence are distinct. Failure to establish bars does not establish full raster. Missing/failed reads or deadline exhaustion yield Unknown. This first experiment deliberately has no remembered geometry or temporal transition control.

The independent worker has one source slot total, including active work, a 250 ms sampling cadence, a cooperative 2 ms measurement deadline, a 24,000-sample cap and a 100 ms queue-age cutoff. Busy/contended offers are dropped. Sampling and logging occur on a lower-priority thread. Source ownership remains attached to its identity and is released on the worker. There is no result getter or path into cropping, presentation, subtitle preparation or existing detector state. Thread creation failure disables this optional experiment. Shutdown alone joins the worker.

## Run

Build the x64 Release host and renderer together. Set `VP_AR_EXPERIMENT=shadow` in the child process environment before launching that candidate. With the variable absent or different, no worker starts. No config setting is required. Do not mix the new renderer with a host from another build. Deployment to `C:\Videoprocessor\vp` is a separate requested action; do not overwrite operator settings to enable the experiment.

`[AR-SHADOW]` records appear in the ordinary VP log. Each record includes source/generation/capture timestamp, format/domain/viewport/renderer identity, rectangle, classification, deadline/read outcome, score components, sample count, worker wall time/queue age, dropped offers and maximum measured submission cost. `kind`: 0 Unknown, 1 Bars, 2 FullRaster. Each edge tuple is depth, coordinate spread, strip support, contrast, noise, darkness, score. `baseline_current=1` requires matching current source sequence and analysis domain; stale baseline fields must not be treated as a same-frame comparison. Baseline classification uses the existing enum (0 unavailable, 1 full raster trusted, 2 bar crop trusted, 3 provisional).

## First playback pass

Use the familiar Eternals scope scenes and the known subtitle-heavy reproduction. Include dirty/near-black bars, ordinary bright full-frame content, dense/changing captions, volume UI, genuine aspect transitions and Off mode. Record timestamps and what the picture/subtitles actually did. This candidate should look like its base build: shadow geometry is only logged. Compare logged source-pixel boundaries with actual frames; do not infer correct detection from CIH fitting or full-input presentation.

Before granting the experiment crop authority, examine misses, false crops, caption intrusion, measured edge error, Unknown frequency, full-raster false positives and transition behavior. Correlate total capture/render/present critical-path cost, current-source submissions, source/presentation gaps and latency with an experiment-disabled run under matched conditions. A synthetic benchmark, a worker deadline or zero renderer drop counters alone cannot establish live cadence. Four-Hz sampling cannot diagnose every individual frame transition.

## Known limits / next decision

This is a small spatial hypothesis scorer, not yet a calibrated multi-frame confidence model. It still has explicit minimum strip support and acceptance thresholds. Dark scene structure can resemble a matte; a stronger interior edge can defeat the true boundary. Wide captions/overlays can contaminate enough strips to defeat measurement. Very thin bars, boundaries beyond the outer third, fades, arbitrary asymmetric crops and non-neutral mattes are not fully covered. Range interpretation is explicit, but code-space darkness is not transfer-function-normalized. Standard-ratio support is reporting-only until evidence warrants giving it decision weight. Full-extent evidence is sparse and must be challenged with lifted/noisy mattes. No claim of live superiority or subtitle safety is established by the initial tests.

Keep these limitations visible in review. First collect real comparisons; then choose the smallest warranted change, including whether spatial evidence is sufficient to trial a replacement while retaining independent subtitle pixel-preservation contracts.

## Focused tests

After building the static library, run `tools\test_experimental_aspect.cmd` from Windows. It compiles optimized x64 tests against the actual detector and AnalysisLumaSource implementation. Spatial cases cover dirty bars with bottom captions, unsnapped geometry, pillar/windowboxing, positive full extent, dark/gradient ambiguity, failed reads, expired deadlines, sample caps, full-range lifted matte and small/large rasters. Worker cases cover opt-in/default-off, native P010 measurement, source identity and ownership, rejection while optional work is blocked, and worker-side release. The deliberate blocked logger is a test-only delay, not a production wait.
