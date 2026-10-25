# VP-0209 / VP-0210 qualification - 2026-10-24

Source: billslack2/videoprocessor commit `7dc9af666b6ca1e004601fee10a4b6d5c10dfe41`, branch `codex/display-statistics-20261024`. Clean source, pushed; not deployed or merged. PR #197 remains draft and unchanged.

Matching x64 Release host, standard renderer, beta renderer, observer and tests built successfully at 21:17:55 EDT. Focused native suite: 278 passed at 21:18:15 EDT, including the actual collector startup/reset/failure/cleanup test. Reusable runner: 98 passed (subset of 278).

## Artifact SHA-256

| Artifact | SHA-256 |
| --- | --- |
| `VideoProcessor-GUI.exe` | `206411AD1F6ADA8251BE65B66DDC58F9715D4EAA061C75AFCF8D10403810A0B6` |
| `vprenderer\VideoProcessorVPRenderer.dll` | `60B12486797410FBD2DCF68F2BB1CD642973E4B4C36CF41D0E3997B7B70D4D34` |
| `vprenderer-beta\VideoProcessorVPRendererBeta.dll` | `9C6E759C3BB057C888588483AC2DE0590DFD62F76E7C526E96F1A7EFFA498882` |
| `vprenderer\VideoProcessorDisplayObserver.exe` | `62CBB5B3BD423140D650E7A9688185AF1BC79484284EBC5460150776AC9FE0D8` |
| `vprenderer-beta\VideoProcessorDisplayObserver.exe` | `62CBB5B3BD423140D650E7A9688185AF1BC79484284EBC5460150776AC9FE0D8` |
| `VideoProcessor-Test.dll` | `A24A6FE94384864F1824BE8DCB28BA61D74345948608F9E5E84439706BC335E5` |

Runtime source sidecars match the clean commit; each binary has x64 PE machine identity. Standard/beta observer copies and licenses match. Installed EXE, both installed renderer DLLs and VideoProcessor.cfg match their pre-work hashes; original VP process remained running.

## Measured overhead

- Final producer microbenchmark: 201 batches of 128 submissions, x64 /O2; batch-average p50/p95/p99 61.7/70.3/74.2 ns per call. Includes two QPC calls, intent construction and submission; correlation is outside the measured interval. This is not individual-call tail latency or an end-to-end bound.
- Full queue with absent consumer: every measured submission immediately rejected; batch-average p50/p95/p99 47.7/47.7/48.4 ns. Measurement coverage is dropped, not playback.
- Actual collector-only off/on/on/off run: four 20-second windows. Helper CPU 0.456% and 0.614% of one core (mean 0.535%); about 9.1 MB working set. Zero reported ETW event/buffer losses or completed-ring overflows; clean helper/session retirement.
- Existing player render averages by window: 4.43/4.20/4.16/4.12 ms. GPU frame averages: 5.97/5.67/5.61/5.62 ms, roughly 49.6-49.7% timing coverage. No measurable uplift; changing workload means this is not a causal improvement or a no-impact guarantee.

## Acceptance limits

The paired experiment observed the unchanged deployed player (592d890b) with the new collector; it did not run the integrated candidate. Source and display were approximately matched at 59.94/59.95 Hz. Integrated playback and mismatched-refresh Smooth Motion acceptance remain outstanding. No physical-scanout or subtitle visual success is claimed. No installer/package was produced.

Drops require confirmed unexpected discard evidence. Repeats/glitches are observed excessive-hold lower bounds; unknown, stale, missing and ambiguous outcomes are exposed as incomplete coverage. The renderer does not wait for collection and does not change Present flags or GPU/source processing.

## External receipts

Root: `E:\codex\diagnostics\vp-display-statistics-20261024`. Raw logs/traces and runtime information stay outside Git.

- `audit-release-build-result.json` and `audit-release-build.log`
- `output-regressions-result.json`, `output-regressions.trx`
- `public-runner/display-statistics.trx`, `public-runner-console.txt`
- `final-artifact-verification.json`
- `PassiveDisplaySubmitBench-final-7dc9af6-results.txt`
- `ab-passive/20261025T011048Z-3863df7f/summary.json` and phase/protocol/telemetry receipts
- `research-playback.md` and pinned source provenance in the source repository

## Subsequent authorized deployment
The previously qualified artifacts were deployed for testing at 21:43 EDT, 2026-10-24, following explicit user authorization. Source remains unmerged. New VP PID56520 and matching renderer identity were verified; observer PID33220 has an active owned ETW session. Deployed file hashes match this receipt and configuration is unchanged. Rollback backup: C:\Videoprocessor\vp\backups\display-statistics-20261024-214333. See deployment-result.json and deployment-runtime-verification.json in the external receipt root. This supersedes the earlier undeployed status; visual playback acceptance remains pending.
