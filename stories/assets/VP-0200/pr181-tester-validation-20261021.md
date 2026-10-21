# PR #181 tester validation — 2026-10-21

## Provenance and limits

The user supplied `vp-log-pr181-2026-10-21.zip.txt`, a ZIP containing one
4,111,501-byte `vp.log`. Raw log/config content is not committed. The tester
reports PR #181 at `4fd790ce` on beta `88289699` with their open PRs on top,
RTX 3060 Ti / driver 511.17 / 200% scaling. Log line 6 and all renderer build
identities report the same combined clean local build
`07ab5fffc593fdb77e2d07a38786d33e193f4a60`, branch `anevard/local-build`, dirty=0.
The log independently identifies Windows 10 build 19044 and the RTX 3060 Ti
with driver `30.0.15.1117`. The inclusion of #181 and 200% scaling are tester
provenance, not independently established solely by that build hash.

The projector was off; the tester used TightVNC. This validates Windows mode
read-back and window/swapchain geometry under their combined build. It does
not qualify projected picture, independent receiver signal timing, visual 1:1
pixel mapping or native OSD size, nor isolated pristine #181 or newer-beta
integration. No new test execution or hardware switching was performed in this
analysis. Log times below are recorded strings; their timezone is unspecified.

## Results reproduced from the log

| Check | Result | Evidence lines in original vp.log |
| --- | --- | --- |
| 1080p59.94 GDI requests | 6/6, each attempt 1/8, result=0 and switch verified | 2331–2340, 6946–6955, 7684–7692, 9286–9295, 10392–10400, 11147–11155 |
| 1080p post-mode client/swapchain | 1920x1080 / 1920x1080 for all six | 2340, 6955, 7692, 9295, 10400, 11155 |
| 2160p23.976 CCD requests | 5/5 refresh-only requests verified, client/swapchain 3840x2160 | 226–232, 4664–4667, 6418–6421, 8733–8736, 12166–12169 |
| Restore on stop | 6/6 GDI restores verified to 3840x2160@23.976 | 4508, 7543, 8579, 10248, 11006, 12009 |
| Mode unverified/rejected/rollback/failure messages | 0 | Entire log; restricted to display-mode/refresh-rate diagnostics |
| Improved fallback ordering | 2160p29.97 precedes 1080p50 and 2160p30 in all six 59.94 eligible lists | e.g. 2330, 6945, 7683, 9285, 10391, 11146 |

The six 1080p candidate-to-verification timestamp differences are respectively
1, 0, 0, 1, 1 and 0 seconds at one-second log precision. This is consistent
with the tester's approximately one-second report, rather than a precise
source-change-to-projector latency measurement. All first candidates succeeded,
so arbitrary fallback application and rejected-mode rollback were not exercised.
The five CCD calls include already-23.976 prior modes; do not equate all five
successful requests with observed physical refresh changes.

## Remaining acceptance

- Projector-on picture, independent receiver timing and visual 1:1/native OSD sizing.
- Rejected-mode rollback, external changes, custom timings and non-primary displays.
- DirectShow/VP Renderer handoff and qualification against newer beta integration.

At this analysis, GitHub beta is `71043e192b658c433dc8d888632f4740d1e61b01`.
[PR #181](https://github.com/billslack2/videoprocessor/pull/181) remains open at
`4fd790ce`. The announced [stats OSD raster PR #183](https://github.com/billslack2/videoprocessor/pull/183)
is open at `1669ee64b3ad96d7ec1976179c4b522d9c131ae5`, based on #181's feature
branch at `4fd790ce`; it has not been reviewed or integrated as part of this
log analysis. No source rebase, merge, deployment or active config edit occurred.

## Internal evidence identity

- ZIP SHA-256: `a6742c19a4372a8c18b5c31c42cab942ca97a246c3627333288156afb2362077`.
- vp.log SHA-256: `929e2ac8a15f05197778b74507d2c1e354e30825f678e4e9caebf6300079ed84`.
- Private extracted evidence/analysis: fix worktree
  `artifacts/tester-pr181-20261021/vp.log` and `analysis.json`.
