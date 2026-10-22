# VP-0202: Prevent long-uptime DirectShow reference-clock overflow

## Status

Done (2026-10-22). The developer accepted the deployed x64 Release repair
with "yes this fixed it." [PR #189](https://github.com/billslack2/videoprocessor/pull/189)
merged the isolated fix into `v1.4.02-beta` as
`b656c25f701462b96bb625def8e19350fa4420cd`. The tested source head is
`4a1f58f4910e63a2193e27d04b7a50002fc45072`, based on fresh remote beta
`3746d59e6c51b6056d8eef8443e0039fba978dcb`.

## Problem and repair

madVR playback developed constant repeated frames. The DirectShow reference
clock multiplied an absolute hardware timestamp by 10,000,000 in signed
64-bit arithmetic before dividing by the hardware frequency. At a 1 MHz
hardware frequency, that intermediate overflows after approximately 10.675
days of hardware-clock uptime. A new player process does not reset that
counter. The invalid conversion could leave the graph clock clamped instead
of advancing. This boundary is established by arithmetic and regression
tests; no successful independent live hardware-counter probe is claimed.

Two capture-to-reference conversion helpers also fell back to whole seconds
beyond the old multiplication threshold, losing subsecond precision and
potentially collapsing consecutive captured-frame starts.

The repair uses the existing wide-intermediate `llMulDiv` for the reference
clock, preserving truncation, and retains `U64_MulDiv` for the controller and
source pin at long uptimes, preserving nearest-tick rounding. Timestamp modes,
monotonic guards, hardware-clock authority, queue policy and cadence behavior
remain intact. No PLL, smoothing or additional timing controller is introduced.

## Validation and deployment

- Initial isolated clock repair: 93/93 focused native regressions passed.
- Accepted deployment: complete x64 Release solution build and 120/120 focused
  regressions passed at combined-source commit
  `92e50dbf707982d3144cfe8966871695f640a4ea`. The previously deployed subtitle
  handoff overlay was preserved in this build and is excluded from PR #189.
- Deployment at 10:24:36 AM EDT on 2026-10-22 replaced the host and both renderer
  binaries with their matching runtime metadata. All deployed hashes matched
  build outputs. Configuration and managed state were preserved without edits.
- Previous files are recoverable under
  `C:\Videoprocessor\vp\backups\madvr-clock-overflow-20261022-102436135`.
- The developer explicitly confirmed the live madVR result: "yes this fixed it."
- Final isolated merge head `4a1f58f4` built the native x64 Release test target
  successfully and passed 93/93 focused regressions on the latest beta base.
  The clean source was merged through PR #189. This merge did not redeploy or
  replace the accepted combined-source installation.

Regression coverage includes the old overflow boundary, multi-day epochs,
representative clock frequencies, distinct Clock-Smart starts, fractional
rounding, duplicate/backward samples, temporary hardware-clock failure and
the existing base-clock clamp. These focused runs are not a full native-suite
claim.

Evidence: [validation and source pins](../assets/VP-0202/clock-overflow-20261022/merge-validation.json),
[deployment receipt](../assets/VP-0202/clock-overflow-20261022/deployment-receipt.json),
[initial 93 tests](../assets/VP-0202/clock-overflow-20261022/clock-fix-regressions.trx),
[deployed-source 120 tests](../assets/VP-0202/clock-overflow-20261022/combined-deployment-regressions.trx),
[final merge-head 93 tests](../assets/VP-0202/clock-overflow-20261022/clock-merge-current-beta.trx).

## Related work

VP-0138 addresses different Clock-Smart2 scene-timestamp ownership. VP-0148 and
VP-0148-2 exclude their separate global wall-clock conversion concern from
queue-contract and telemetry work. This record covers the DirectShow hardware
reference clock and its two capture timestamp adapters. Existing VP-0138,
VP-0189 and subtitle/AR story states are unchanged.