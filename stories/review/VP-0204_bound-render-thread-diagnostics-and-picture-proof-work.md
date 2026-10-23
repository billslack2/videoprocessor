# VP-0204: Bound render-thread diagnostics and picture-proof work

## Status

Review

Implemented and pushed as source commit
`3f5dd8a1b2693e79645bab4dfdf24e76fba53cc8` on
`codex/render-thread-jitter-20261022` in
`E:\codex\videoprocessor\render-thread-jitter-20261022`.
Draft [PR #192](https://github.com/billslack2/videoprocessor/pull/192) targets
`v1.4.02-beta` from verified baseline
`fcc33fc9a3b57ce79c56904b3ff78b23f27bfe02`: two implementation commits, 31 files.
LUT Apply-only PR #191 and VP-0203 remain separate and untouched.

The final committed-source x64 Release solution build passed with zero errors
and zero warnings. After the measurement-driven checkpoint correction, final
native validation passed 1,023/1,023 tests; the isolated measurement DLL also
passed. The unchanged current-beta Windows Config suite has 108
registered tests: 104 passed and four unrelated failures were confirmed on clean
baseline `fcc33fc`. The two extra Config tests in LUT PR #191 are excluded from
this branch; this result is not a 110-test run.

Ready for PR review. No merge, deployment, player restart, active configuration
change, or live-input timing acceptance is claimed. Runtime jitter comparison
requires separate authorized deployment and observation of the new counters.

## Implemented behavior and independent review

- Logging uses fixed-size producer records and bounded queue admission. Writer
  timestamp formatting, file I/O, flushes and self-reports occur outside the
  queue lock; producer contention/overflow and writer flush cost are observable.
- NVIDIA BT.2020 driver work runs on its owning worker without holding the
  shared request/status mutex. Generation-bound requests and cached snapshots
  preserve latest intent, restoration debt, and shutdown ownership.
- Optional proof receives an explicit caller-owned cooperative budget. Samplers
  check bounded batches/rows and unwind using a dedicated cancellation exception.
  The renderer discards the incomplete local prefix before authority publication.
  Mandatory live safety calls retain the default unbounded/null-budget path.
- One source-output batch reuses exact raw/global/AR-darkness measurements, even
  for a single output. Compatible completed worker results seed it only after
  full identity/domain, source layout/interpretation and backing ownership match.
  Each distinct retention base still inspects its excluded bands. Incomplete
  optional evidence and caption-refined copies do not become independent proof.

Principal-engineer, threading-architect, and independent evidence/cache reviews
covered concurrency, resource lifetime, cancellation and regressions. Findings
corrected before final validation include preserving old-target dirty/restoration
ownership across superseded NVAPI intent, avoiding late blocking diagnostic-lock
acquisition, and eliminating extra raw/global scans for uncached borrowed sources.
The latter path reuses the caller's exact current raw/global observations without
weakening the rule against caching borrowed pixels.

The cancellation review verified that budget-capable helpers finish before the
post-proof expiry/queue-publication boundary; worker lambdas do not capture the
stack budget. Deterministic regressions cover mid-scan cancellation, absence of
partial authority, whole-window/certificate rejection, and successful mandatory
retry on the same pixels. The authority-adoption API remains unchanged.

## Measurement follow-up and checkpoint correction

Real-clock measurements of initial source `ead594028431e4c1c43b5886e4da437a8f066444`
found three direct samplers bypassing the cooperative checkpoints: strict bar
profile samples, disputed expansion strips, and credible-visible-extent samples.
The follow-up adds `samples.Checkpoint()` before those direct reads. A new
`DenseVisibleExtentChecksBudgetBeforeReturningCompletedRetention` regression
expires at checkpoint 40, verifies that no partial safety result returns, and
proves a subsequent mandatory retry remains equivalent. The earlier raw
measurements are preserved under `measurements/before-dense-checkpoint-fix`.

The committed report `docs/VP-0204_MEASUREMENTS.md` and four aggregate CSVs under
`docs/measurements` record the methods, distributions, costs and limits:
`VP-0204-analysis-reuse.csv`, `VP-0204-analysis-budget.csv`,
`VP-0204-logger-summary.csv`, and `VP-0204-nvidia-aggregate.csv`.

- Real QPC analysis measurements use 101 interleaved pairs per case across 15
  fixed 4K fixtures. In the three dark-textured 1 ms cases, between-helper p95
  deadline lateness was 0.4233-0.5285 ms; cooperative p95 lateness was
  0.0395-0.0529 ms after the correction. Cooperative worst observed lateness
  was 0.1026-0.1334 ms in those cases, not a hard scheduling or deadline bound.
  Checkpoints and unwinding cost time: at the 2 ms BGRA8 limit, 66/101 complete
  proof windows versus 86/101 for the between-helper control is a documented
  tradeoff. No cancelled prefix was published; mandatory current safety remains.
- With an exact already-ready worker result, the measured caller helper-stage
  median was 20-26% lower for scope and 61-64% lower for dark-textured cases.
  Worker measurement is outside that caller timer and still consumes CPU.
  Fallback-only changes remain small and mixed; this is not whole-frame,
  whole-process, GPU, or live-playback savings.
- Five logging trials per controlled case made 1,000 calls before a deliberately
  held 100 ms dependency gate was released. Periodic-flush transport p99/max
  were 0.7/1.6 microseconds; the extracted original QueueMessage path under
  periodic/final flush lock held callers for 104.5-108.8 ms. These are injected
  stalls and transport results, not observed production disk stalls or full
  Log() latency. Contention rejection and extreme-saturation losses are recorded
  explicitly; they are not a production log-loss estimate.
- The fake-NVAPI harness completed 28/28 blocked trials safely, including stale
  generation checks. Request/Status p99 were about 0.1/0.2 microseconds while
  the old driver-held-lock control blocked its first caller about 109 ms.
  No actual NVAPI call or display state change was performed by this harness.
- Passive installed-build evidence remains the healthy 22:17:57-22:19:57 window:
  7,199 submissions, active CPU 4.180 ms average / 6.903 ms peak, and zero
  source/display/preview overruns or reported backlog/drop events. Existing
  4K capture playback continued while scratch measurements ran, so elapsed
  timings include shared-host scheduling/cache/memory effects. No live jitter
  improvement is established; controlled matched replay remains future work.

## Validation evidence and limits

Artifacts are under
`E:\codex\videoprocessor\render-thread-jitter-20261022\artifacts\vp-0204`:

| Check | Result | Evidence |
| --- | --- | --- |
| Final committed-source full solution x64 Release build | Source `3f5dd8a1`; passed, 0 errors, 0 warnings | `measurements/release-3f5dd8a1.log` |
| Final native regression suite after direct-sampler correction | 1,023/1,023 passed | `measurements/results/dense-fix-native.trx`, `measurements/dense-fix-native.log` |
| Initial focused native suite | 1,021/1,021 passed | `native/focused.trx`, `native-tests.log` |
| Isolated final measurement DLL | Passed | `measurements/analysis-final-run.log` |
| Initial paired 4K fixture test | 1/1 passed; all evidence parity checks passed | `native/paired-4k.trx`, `paired-4k.log`, `paired-4k-summary.json` |
| Prior Windows Qt Config suite; UI unchanged | 104 passed, 4 failed out of 108 | `config-tests.log` |
| Final source/validation manifest | Exact source `3f5dd8a1b2693e79645bab4dfdf24e76fba53cc8`, native 1,023/1,023 and isolated 1/1 | `validation-summary.json` |
| Additional UI baseline comparison | Both cases fail identically on clean baseline and feature; each exit 1 | `config-baseline-comparison.log`, `config-baseline-comparison-results.json` |

The warmed paired test covers 15 synthetic 3840x2160 fixtures: P010, P210 and
BGRA8, each with scope, full-raster, dark-textured, subtitle-edge-panel and
expansion patterns; 21 pairs per fixture. Fallback uses one raw/global/AR-darkness/
retention scan each; exact worker import requires no raw/global/AR rescan.
Fixture preparation is excluded. Fallback wall-time changes are small and mixed;
these tests establish evidence parity and scan reuse, not reduced live jitter.

All four UI failures are baseline-confirmed:

1. `subtitle background shape round trip and visibility`:
   `Loading shape defaults dirtied configuration`.
2. `subtitle HDR legacy fallback and shared modes`:
   `Loading inherited HDR settings dirtied the profile`.
3. `two-column cards share row height`: `Missing card: Display`.
4. `primary discovery selectors open on field click`:
   `Missing control: config.general.fullscreen_monitor_name`.

The first two were run serially with `QT_QPA_PLATFORM=windows` and exact `--test`
selectors against clean baseline `fcc33fc` and clean feature `ead59402`; their
per-target logs and source identities are saved beside the comparison log.
The other two were independently reproduced with the same Windows Qt platform
using the clean `fcc33fc` ConfigTests build during VP-0203 validation. Baseline
logs remain under
`E:\codex\videoprocessor\render-thread-audit-20261022\artifacts\vp-0203-baseline`
as `two-column-cards-baseline-test.log` and
`primary-discovery-baseline-test.log`. No unrelated UI fix is included.

Diagnostic interpretation and ownership are documented in source
`docs/VP-0204_RENDER_THREAD_WORK.md`. `VP async log telemetry`,
`NVIDIA BT.2020 worker`, `Alpha optional proof budget`, and
`VP source measurement interval` provide low-rate evidence for later controlled
runtime comparison. Deployment and active configuration remain untouched.

## Objective and scope

Reduce avoidable render-thread stalls without weakening current picture,
subtitle, crop, resource-lifetime, or presentation-safety decisions. Implement
the following four findings together in one separate reviewable PR:

1. Logging: remove writer file flush/I/O from the producer queue critical
   section; keep producer work and queue growth bounded and expose dropped or
   contended diagnostics without recursively logging from the logger.
2. NVAPI telemetry: prevent slow vendor/driver sampling from holding a shared
   telemetry mutex that a render/UI consumer needs. Publish short-lived result
   snapshots while respecting vendor API and worker lifecycle requirements.
3. Optional picture-proof deadlines: honor the available deadline around
   expensive evidence work, return unavailable/deferred on exhaustion, and
   never promote partial work to authoritative picture or subtitle proof.
4. Duplicate pixel measurements: reuse exact immutable per-frame evidence
   across compatible preview/live requests instead of scanning the same pixels
   again. Keys and invalidation must distinguish source/frame/format and all
   measurement-affecting policy or geometry inputs.

## Boundaries and related work

- [VP-0203](../review/VP-0203_load-calibration-lut-disk-changes-only-on-explicit-config-apply.md)
  and its [PR #191](https://github.com/billslack2/videoprocessor/pull/191) remain
  untouched. LUT Apply-only behavior is not included in this branch or PR.
- [VP-0124](../in-progress/VP-0124_safe-outward-active-picture-lookahead.md)
  records the older outward-lookahead feature investigation. This task bounds
  the existing beta's work and preserves its decisions; it does not reopen
  feature enablement or change crop policy.
- [VP-0140](../review/VP-0140_compile-vp-renderer-shaders-without-blocking-ui-or-presentation.md)
  owns shader preparation. Shader compilation architecture, screenshot readback,
  new GPU-context owners, capture transport, and broad renderer extraction are
  outside this task.
- No deployment, player restart, configuration/LUT edits, or acceptance of live
  playback timing is implied by software implementation and automated tests.

## Readiness and source identity

The root session verified current remote/default beta `v1.4.02-beta` at
`fcc33fc9a3b57ce79c56904b3ff78b23f27bfe02`. The new source worktree was verified
clean at that exact commit before implementation:
`E:\codex\videoprocessor\render-thread-jitter-20261022`, branch
`codex/render-thread-jitter-20261022`. Existing source worktrees and LUT PR #191
are preserved.

The static audit identifies producer/writer queue-lock coupling, shared NVAPI
sampling-lock coupling, deadline-insensitive evidence helpers, and duplicate
per-pixel evidence requests. The implementation keeps existing owner-thread
resource lifetimes, uses bounded immutable telemetry/evidence publication, and
returns safe unavailable results when optional work cannot finish. Independent
principal-engineer and threading-architect reviews cover ownership and tests.
No new library or capture hardware API is required.

## Runtime evidence and measurement limits

The reviewed deployed logs identify host/renderer `6e53a6e`, which is not an
ancestor of source baseline `fcc33fc`; those measurements motivate work and are
not current-branch performance acceptance. On 2026-10-22 at 19:11:46,
`C:\Videoprocessor\vp\logs\vp.log.0` records 48/48 preview attempts incomplete
and overdue, average 2.758 ms / peak 3.567 ms, with unknown presentation phase
and a 1 ms fallback allowance. Actual 24 fps scanout drops are not established
by the display-period counter. The same log has 117,594 lines over 4,881 seconds;
logger traffic is real, but logger or NVAPI causation of a visible stall has not
been measured. Keep software/runtime attribution explicit.

## Acceptance and validation

1. Deterministically hold or delay a writer flush and prove producer queue work
   makes bounded progress without waiting for file I/O; preserve shutdown,
   ordering/retention, external sink, and observable overflow behavior.
2. Delay the vendor sampling operation in a test seam and show consumers obtain
   a coherent latest snapshot without waiting for that operation. Test start,
   stop, empty/failed sample, and publication/lifecycle races as appropriate.
3. Exhaust deadlines before and within expensive optional proof; assert that
   no partial evidence becomes authority and mandatory current-frame safety
   decisions retain their baseline semantics.
4. Verify evidence reuse for the exact same immutable frame and cache misses
   for changed frame/sequence/generation/format or relevant measurement inputs.
   Test success, unavailable, cancellation, reset, and worker/live handoff.
5. Use representative captured or synthetic SDR/HDR, planar/native-format,
   letterbox/full-raster, near-black, subtitle/OSD, and changing-geometry fixtures
   where relevant. Compare output/decisions and actual scan/work counts, rather
   than tests that only duplicate implementation structure.
6. Record appropriate focused regression results, a successful x64 Release
   build, exact source identity, and independent review. Any broader existing
   failures must be distinguished with baseline reproduction where needed.
7. Add low-cost counters or sampled timing for deadline deferral/overrun,
   evidence reuse/miss/work, and telemetry/producer contention where useful.
   Avoid new per-frame text or synchronous I/O. Report performance fixtures and
   measurements honestly; no improved live jitter claim without runtime data.

## Tracker audit

Fetched origin/main `08c2595c726bcf2841188c12411befbc05fd16bc` contained
222 canonical records and index rows: 201 roots, 21 children, highest root
VP-0203. Registry Last VP-0203 / Next VP-0204 / count 222 matched. No duplicate,
missing, orphan, or non-contiguous child IDs were found. No open canonical story
covers this four-finding scope. Historical VP-0070/VP-0088/VP-0168 status-format
exceptions remain reported and untouched. This task does not modify VP-0203.

Review-transition audit after fetching origin/main
`0f55426ed11c7e443676006282f4551b8cf1928f` found 223 canonical records and index
rows (202 roots, 21 children), highest root VP-0204, matching registry Last
VP-0204 / Next VP-0205 / count 223. No new duplicate, missing, orphan, child-order,
or state discrepancy was found. Known VP-0070/VP-0088/VP-0168 formatting exceptions
remain unchanged; VP-0203 is unchanged. VP-0204's file, exact Review status and
index row move together in this tracker commit.

Measurement follow-up remains in Review. After fetching origin/main
`2609120c1707a7187319f6ca4a50e73ee68842c7`, the canonical/index audit still found
223 files/rows, 202 roots and 21 children with registry VP-0204/VP-0205/223 and
no new identity/state discrepancies. Exact Review status and index row remain
aligned. VP-0203 and historical formatting exceptions are unchanged. Source is
pushed clean at the updated PR head; no deployment or configuration edit occurred.
