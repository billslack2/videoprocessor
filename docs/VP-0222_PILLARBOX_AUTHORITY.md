# VP-0222: Contradicted pillarbox authority

Netflix menu gutters could acquire a side-only rectangle while a vertical bar proposal remained rejected. Later full-width menu pixels did not necessarily certify the bottom boundary, so the provisional/deferred transition paths retained that rectangle and the OSD continued reporting it.

## Admission and withdrawal

A native side-only crop with a completed but failed vertical bar proposal cannot establish a new whole-picture aspect. Live acquisition, recent-history reacquisition, and queued adoption apply the same veto. Rejected observations cannot accumulate confirmation votes. Established genuine pillarboxes retain their stable geometry through ordinary darkness/ambiguity; clean candidates keep their existing confirmation counts.

A separate withdrawal certificate examines an established side-only crop. The current native scan must reach both horizontal raster edges, both old side bands must contain majority nonblack picture with broad existing luma/texture/continuity support, and three adjacent unique source observations must agree on the exact old crop and source generation. The vertical candidate may remain provisional or jitter by a scan step. Dark frames, partial scans, localized overlays, mismatched rectangles, source gaps, and synthetic authority cannot supply this certificate.

Withdrawal does not certify a replacement aspect. The renderer reveals the full input for the withdrawal frame and uses the existing synchronized post-render reset to clear live geometry, lookahead history, queued geometry decisions, and associated subtitle/presentation evidence. The OSD becomes unavailable until independently measured boundaries establish a new aspect. No ratio is forced or blacklisted, and physical-screen/fitting settings do not participate.

## Validation

Run the native detector, transition, timeline, crop-policy, and OSD test classes. The native positive controls include 1.66-adjacent and 4:3 bars at 1080p/2160p in P010/P210. Negative controls cover unresolved axes, history/queue bypass, confirmation banking, dark scenes, partial overlays, repeated/gapped source identities and generation changes.

For the original 3840x2160 Netflix Home diagnostic capture, set `VP_PILLARBOX_REPLAY_DIRECTORY` to the local capture directory and run `OptionalNativeFullWidthMenuWithdrawsObsoletePillarbox`. It uses original native frames and source sequences, with the explicitly documented prior crop 136,0-3704,2160. No captured pixels are stored in the repository. This component replay proves withdrawal eligibility, not asynchronous live scheduling.

Before deployment, run the x64 Release solution build and `tools/test_subtitle_motion_renderers.ps1` for standard and beta dependencies. Deploy the paired host and selected renderer from the same clean commit, preserving configuration and backups. Physical playback remains separate acceptance.
