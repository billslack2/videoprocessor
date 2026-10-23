# Native paused scope incident evidence

On 2026-10-23 the user reported that pausing and rewinding after a scope scene left a full-height picture cropped to the old scope aspect. The captured process was PID 68268, host and renderer `24e0dd10430e5140c06c1fa1bd106c9bbde61ef7`.

- `incident-summary.json` aggregates the preserved live log through 12:17:01. `matched-extracts.txt` supplies snapshot line anchors; `latest-recovery.txt` preserves a later unchanged sample beyond five minutes. The full log remains in ignored local diagnostics.
- `capture-index-summary.json` describes the native capture at `C:\Videoprocessor\vp\logs\analysis-20261023-122213-68268-1`. `capture-summary.json` and `frame-index.jsonl` preserve only small metadata. No pixel file is included.
- All six sampled full-frame blobs, spanning five distinct accepted identities and one repeated identity, are byte-identical. This proves those samples are unchanged, not every unsampled file. Native data are `DecodedDolbyVision422`, BT.2020, Full range; the sidecar format ID is 4, whereas P210 is 1.
- Native pixels cover 32 unique identities over approximately 1.418 seconds before the near-2-GiB byte cap; the 14,031-record trace covers 29.989 seconds. Missing and repeated frame identities must remain explicit in replay.
- `baseline-policy-identity.json` verifies identical crop-policy and transition-model blobs between current beta `f7c7a820` and deployed `24e0dd10`. The retained-scope veto predates PR193; threshold and input interactions remain under investigation.
- `tracker-audit-before.json` records allocation checks and historical formatting exceptions, without repairing unrelated stories.

The observed 120.078 seconds contain 2,879 preview attempts, 2,878 completions and zero optional deadline overruns; 2,875 invalid outward outcome events produced no proof publication or adoption. These emitted telemetry rows distinguish geometry rejection from budget exhaustion; they do not prove actual display delivery. Capture lifetime drop counters are not per-session loss counts.
