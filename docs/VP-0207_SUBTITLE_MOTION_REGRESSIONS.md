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
