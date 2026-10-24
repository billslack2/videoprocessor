# Subtitle and Smooth Motion regression checks

Run both shipped libplacebo dependencies after changing temporal packet lifetime,
subtitle preparation, or source processing. The normal native test target uses
libplacebo 360; it does not exercise the deployed beta renderer's 377 code.

From PowerShell:

```powershell
.\tools\test_subtitle_motion_renderers.ps1 `
  -MagewellSdkRoot 'E:\codex\tools\MagewellCaptureSDK-3.3.1.1596\SDKv3' `
  -OutputDirectory 'E:\codex\diagnostics\new-motion-regression-run'
```

The script builds x64 Release tests, runs the motion/subtitle GPU classes serially
against standard and beta, and writes build logs, TRX and binary/dependency hashes.
Beta uses distinct objects and output files under `x64/Release/tests-beta`.
`RendererDependencyTests` verifies the loaded library matches the chosen headers.
Native CPU/fixture and host-stack qualification are additional checks; this script
is not a deployment or playback-acceptance command.

## Recovery contract

A forward source gap or explicit backlog recovery must discard every queued
picture, subtitle layer, processed result, callback and selected-source receipt.
It must also flush HDR peak/mixing evidence and clear published HDR status.
Compiled programs may survive on the same GPU. Full teardown, GPU replacement,
policy changes, nonmonotonic timestamps and explicit discontinuity still release
the processing renderer.

Destroying the processing renderer on every backlog recovery caused repeated
HDR program compilation: the resulting stalls triggered more drops and another
renderer destruction. Structural GPU tests assert renderer creation counts,
fresh peak state, current-source pixels, repeat reuse and hard-reset behavior.
They avoid machine-specific timing thresholds. Live `VP source/output processing`
records include renderer creation and history-flush counts to diagnose recurrence.

Same-call provisional subtitle detection may be reused only after unchanged
current-pixel plane proof and when final scan coordinates exactly match. It must
preserve all glyph/card masks and configured companion coverage. The optional
native fixture regression compares the reused result with an independent full
scan and checks exhaustion and changed glyph pixels. Fixture pixels stay local.

Cleanup padding can contain ordinary picture pixels outside the measured card.
A change there is not by itself proof that the caption disappeared. Only the
full current inspection may defer that preliminary mismatch; it must then prove
complete configured coverage and exactly unchanged current glyph, measured-card,
per-line and cleanup geometry. Partial inspection and changes inside the card
remain strict. No old cleanup lease is renewed by incomplete work.

Controlled GPU tests do not establish physical 23.976-to-59.95 Hz playback or HDR
display acceptance. Qualify both uninterrupted playback and recovery from drops,
including Reconstruction, transparent glyphs, source cue changes and no-cue
frames, before marking the live regression resolved.

## Bounded subtitle reuse during Smooth Motion

When a source packet briefly lacks a prepared subtitle, active Smooth Motion may
select one earlier, successfully processed presentation packet for at most two
source intervals. Its cleaned picture, subtitle layer, replacement background,
geometry and HDR state remain together. The fallback does not apply old cleanup
to new source pixels and does not run an additional subtitle scan.

This is an intentional visual tradeoff: a brief picture/subtitle repeat or delayed
cue disappearance is preferable to an isolated subtitle flash. The age limit is
anchored to the original source sequence and timestamp; inserted output frames
cannot renew it. A new prepared cue wins immediately. Incompatible processing,
viewport or geometry, discontinuities, source gaps and renderer resets prevent
reuse. Smooth Motion Off and automatic passthrough retain their existing behavior.

GPU regressions must cover both missing source frames and many inserted outputs,
expiry on the third missing source, immediate new-cue selection, cue-end expiry,
immutable background/layer pairing, reset/context rejection and ordinary mode.
Live diagnostics must distinguish these intentional presentation repeats from
fresh subtitle acquisition or current cleanup authority.
## Transfer and chroma controls

Exercise opaque HLG preparation separately from transparent subtitle layers.
Every full-raster output pixel must be finite, and opaque video must not inherit
stored texture alpha. Real source alpha retains its normal composition policy.
Keep explicit phase/transfer diagnostics on shader failures; optimized release
line mappings can point at the assertion after the failing render call.

A direct P010 reference and the prepared/temporal paths must use the same chroma
scalers. A null plane scaler inherits the main scaler; fast and default rendering
presets therefore do not express the same policy. Check explicit bilinear and
Lanczos policies through all three paths, both field orders and each deinterlacer,
without widening pixel tolerances to accommodate a mismatched reference.


## Complete reconstructed pictures during progressive Smooth Motion

Progressive Smooth Motion reconstructs one complete source picture with the same
All composition used without Smooth Motion, then passes the owned pictures to
libplacebo's frame mixer. Reconstructed glyphs and their replacement background
can blend with the picture. This is an accepted visual tradeoff; it avoids a
separate full-raster subtitle-layer pass and VP's extra processed-picture mixing
and encoding stages. Source tone mapping, HDR peak detection and dithering retain
the configured parameters. The measured HDR detector state belongs to the render
pass, which can include a blend; it is not proof of a new source measurement.

The progressive complete-picture path uses the configured spatial replacement.
It skips the later temporal-donor copy and unused donor capture: copying a donor
onto an already reconstructed picture could erase the relocated glyphs. Native
interlaced composition retains its separate field preparation and layer path.
Smooth Motion Off retains its existing processing.

Completed-picture receipt, source ownership, presentation context and successful
render are required for bounded subtitle fallback. A failed capture, failed render
or repeated output cannot create or renew that receipt. No-caption sources do not
claim a subtitle. The existing two-source-interval limit, new-cue selection,
geometry/context rejection and source-gap invalidation still apply. Reusing a
complete presentation never grants cleanup authority over new source pixels.

Qualification must include actual UHD native detection, placement and GPU
preparation through the production helper before temporal capture and rendering.
Exercise Reconstruction with surround fill, blur, HDR, a glyph-free backing lip,
cue changes, empty cues, repeated outputs, source gaps and context changes on both
shipped graphics dependencies. A small already-prepared texture is insufficient
coverage for this path. Controlled paced timings must compare the alternatives
under the same running-process and GPU conditions. Record cold compilation
separately from warm work, and do not present CPU wall time as shader execution.

## OSD processing costs

The statistics OSD separates actual subtitle-preparation calls from new-source and
reused-output render calls. Each row shows average/maximum milliseconds and its
own sample count in a completed two-second window. These are CPU submission and
driver/GPU wait times, excluding Present/VSync; they are not GPU timestamps. New
and reused draw costs include their active analysis, upload and preparation work.
Subtitle preparation is a subset of that work, so it must not be added again. A
reused output must not dilute the mean preparation cost. No calls and unavailable
measurements are shown explicitly. Mode/context changes invalidate prior figures,
and old publications expire.

The GPU frame row retains its independent ten-second asynchronous measurement
window and shows matched sample count versus submitted output count. Sampling
can miss subtitle-preparation outputs; low coverage or no samples cannot establish
that preparation is free. Never sum CPU/wait rows with GPU frame time. Telemetry
must not introduce a GPU finish, blocking query or render-mutex wait on the UI.
