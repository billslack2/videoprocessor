# Native source-boundary investigation, 2026-11-01

## Scope and source identity

GitHub default/current beta was independently queried as `v1.4.02-beta`, tip
`a591ad3b532771911e85f8ef026958279f2065f0`. A clean worktree was created at
`E:\codex\videoprocessor\native-boundary-investigation-20261101` from that fetched
remote tip. The existing deployed history through `c4c915360e1572650d6b3e85681f8b9a3690bf86`
was then fast-forwarded into this branch. This includes approximately sixty
unmerged commits and their subtitle/runtime dependencies; this is not a standalone
one-commit beta patch. Existing checkouts were preserved. Only the authoritative
`billslack2/videoprocessor` remote was used.

No player stop/restart, GPU test, deployment or configuration edit was performed.
PID60364 was running during investigation, then the runtime log records its
keyboard-close handler and orderly shutdown at20:42:05. It was left closed.
The active state selected classic subtitles, standard_scope display, scope
viewport/zoom and madvr_queue. Those presentation settings supplied no detection
coordinates or preferred aspect.

## Independent evidence

Preserved originals, independent decode output and replay TRX files are under
`E:\codex\videoprocessor\evidence\native-boundary-20261101` (private pixels are
not committed). This includes the initial57-frame20:21:48 capture, a newly found
93-frame20:35:10 capture, rotating logs, shutdown log, and configuration snapshots.

Sidecars resolve to native V210/REC709/limited range:3840x2160,10240-byte rows,
22,118,400 bytes per frame. Independent little-endian unpacking agrees with the
production sampler at all1596 sampled luma points across the first57 frames.
This proves those values/layout interpretations, not all possible conversions.

All150 frames have their first broadly bright source row at7 and last at2159
under the independent diagnostic threshold Y>100. In the first57, row6 never
exceeds80, row7 is at least244 everywhere, and bottom-row pixels are at least388.
This threshold is diagnostic evidence, not a new product threshold. Native
measurement additionally proves the four boundaries through its own distributed
edge, continuation and terminal checks. Presentation screenshot borders were not
used as source geometry.

Baseline replay with the same extracted black reference as production reproduces
`physical-raster-current-edge-unresolved` on frame1 (736 samples). The older
optional replay omitted that reference for non-full-raster expectations and
instead reported `physical-raster-black-reference-unavailable`; the harness now
supplies the production reference consistently.

## Established logic failure and correction

The legacy physical observer fitted terminal matte from offsets0,2,4 and required
holdouts6,8. For a seven-row dark margin, row8 is picture, so the reference fails
and candidate search is skipped. Terminal fallback then requires picture pixels
at offsets0,1,8, which the real margin cannot provide. Fine search also excluded
one/two-pixel margins. These are conflicting spatial contracts; temporal holds
cannot fix them.

The correction measures shallow dark-prefix candidates before fitting the same
reference, keeps reference samples outside the candidate, and runs the existing
coarse/fine, weak-collar, continuation, exterior-contradiction and terminal proofs.
It adds no separate rectangle admission path. Fine bounds respect leading and
trailing sample directions. Shared P010/422 chroma footprints are excluded only
where they cross the candidate; where no exterior chroma is observable, source
luma supplies shallow geometry. Available exterior color remains checked.
Sustained faint-prefix evidence vetoes reference refitting rather than being
absorbed into a new matte level. Search remains budgeted and source-local.

The existing spatial detector already measures[0,7..3840,2160] on all57 original
frames through the bounded worker, without this correction (zero mismatches,
peak2.350ms including active verification and darkness evaluation). Its crop
verifier remains provisional with full-source trusted bounds. Measurement and
crop authority are separate: no crop verifier was relaxed here.

The earlier20:18:31 event is confirmed only as authority divergence: legacy main
crop evidence was provisional/retention_safe=0 near[0,6..3840,1970], while physical
reporting accepted[0,7..3840,1969],1.957187, and retained it through Unknown.
Applied AR stayed1.777778. Both available captures belong to later PID60364;
no matching native source for that earlier process/event was established.
Its true rectangle and root cause therefore remain unproven.

## OSD contract

Current measured bounds or Unknown remain visible alongside a separately labelled
retained estimate. Current legacy/spatial provenance is independent of the retained
producer. Applied AR still follows successful presentation submission. The anchored
3-percent retained-history policy remains unchanged. Native tests verify the
formatting and source-reset/provenance contracts; live text fit remains untested.
Ordinary startup still selects legacy unless the startup environment enables spatial
mode; this investigation does not silently change the selected detector.

## Verification

- Failing-first native margin regression recorded before correction.
- Final complete Native lane:981/981 passed, run
  `artifacts/qualification/runs/20261101-204340-db8b7f83`.
- Exact margins1..9 on all four sides in P010/P210, including varying shared-pair
  chroma; sustained faint collars3/10 on every side; noisy partial top edge,
  localized exterior contamination, repeated immutable frames and interior dark
  strip with picture outside are covered. Existing subtitle, partial-edge,
  interior-boundary, source-identity and60Hz skipped-analysis contracts remain in
  the complete CPU run. Shared CPU tests were executed once, not per renderer.
- Corrected legacy reporter with production2ms budget:57/57 and93/93 exact
  `[0,7..3840,2160]`, zero mismatches. Peaks0.559ms and0.462ms respectively;
  these are local replay measurements, not a guarantee on all hardware/content.
- Reported aspect is3840/2153, approximately1.78356, without standard-ratio snapping.
  Conservative retention bounds remain unchanged. No temporal rule was added.
- Two independent reviewers audited measurement and pipeline/OSD separately.
  Their findings led to the persistent weak-prefix veto and format-aware chroma
  footprint tests. Final reviews found no blocking issue within their scopes.

To reproduce CPU validation:

```powershell
.\tools\qualify_runtime_build.ps1 -Lane Native -MagewellSdkRoot E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3
$env:VP_PHYSICAL_EDGE_CAPTURE_DIRECTORY='<preserved capture directory>'
$env:VP_PHYSICAL_EDGE_CAPTURE_EXPECTED_EDGES='0,7,3840,2160'
# Run PhysicalSourceEdgeDoesNotBecomeItsDecodedHaloRetentionEnvelope using VSTest
# against x64/Release/tests-critical-native/VideoProcessor-CriticalNativeTests.dll.
```

## Bounded remaining work

This is a CPU-verified measurement/reporting change, not runtime qualification.
No GPU integration or OSD layout check was run. Existing Black/reconstruction
queue-gap failures remain separate. The native recorder subsamples source frames
(step13), so these corpora do not prove every-source59.94/60Hz dynamic scope/IMAX
transition timing.

1. Locate matching source pixels before attributing the earlier1.96 geometry;
   otherwise reproduce with a bounded session/sequence-bound native capture.
2. When GPU work is permitted, build the matching x64 Release host and both
   renderer DLLs and execute the relevant actual renderer/Classic tests. Verify
   OSD fit and current/retained/applied correspondence from the same source IDs.
3. For a green rectangle, first validate an offline full-input-raster view with
   exclusive native bounds. A future opt-in runtime full-raster inset can preserve
   coordinate meaning under crop/zoom/CIH; show qualified current geometry and label
   retained estimates separately. No unverified overlay was added now.
4. Deployment requires the established runtime qualification/manual-review process,
   a matching Release runtime pair and backed-up minimal configuration edits if
   needed. Do not deploy based only on these CPU results. Preserve the current
   subtitle path and verify actual high-rate dynamic transitions afterward.
