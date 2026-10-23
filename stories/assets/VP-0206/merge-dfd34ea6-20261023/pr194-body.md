After rewinding from Scope into a full-height picture with one dark side, pooled perimeter calibration could mistake scenery for a raised black level. The captured frame produced a false 936-pixel left-bar proposal and kept the previously submitted Scope crop.

Qualify raised mattes across the existing full-band depth samples, preserve uncertainty for genuine noisy or obscured mattes, and accept nominal Full-range geometry only with distributed support on all four outer lines. Acquisition and retention share the selected calibration. This adds no source pixel reads. Fixed-layout capture records and rate-limited logs explain the selected black reference, supporting edges and actual publication. Very flat margins within the supported matte-noise envelope remain provisional.

Validation of exact feature commit `c48eb134c34a93c762e026af6daf50aaeb1dc83c`, independently checked on current beta `f7c7a8204f7a2cace68b3b40774406680af6c0d8`:

- Clean serial x64 Release solution Rebuild passed, including host and both renderer variants.
- 721/721 relevant native picture-analysis, black-reference, transition, crop-recovery, retention and final-fill regressions passed.
- Actual 64-file native capture replay passed: 32 unique identities, 32 repeats and two gaps. Presentation releases on source 10694; logical full-raster geometry confirms on 10700. Captured identities are unchanged; historical Scope acquisition is an explicitly synthetic seed.
- Independent principal source review found no dependency on the other draft PRs and no remaining source blocker.

The user confirmed successful live playback testing of the locally deployed integration `4d1d62d0` on 2026-10-23 and authorized this merge. That installed integration retains separate logging/fullscreen/NVIDIA/lookahead work; this PR contains only the seven Scope-fix, diagnostics and test files. Merging it does not replace installed binaries or configuration.

The earlier unfiltered full-DLL sweep was interrupted and has no completed result; no full-suite pass is claimed. The standalone checks above completed successfully. Tracks VP-0206.
