# VP-0204: Bound render-thread diagnostics and picture-proof work

## Status

Review

Draft [PR #192](https://github.com/billslack2/videoprocessor/pull/192) is unmerged
at `6e7ce51103269a5fd27ac5adcaa40c3b12a3635a` (the PR branch remains
`codex/render-thread-jitter-20261022`; the logging worktree is
`E:\codex\videoprocessor\log-memory-20261022` on `codex/log-memory-20261022`).
Its verified current/default beta base is `v1.4.02-beta` at
`fcc33fc9a3b57ce79c56904b3ff78b23f27bfe02`. PR #191 / VP-0203 and broad
foreground-policy VP-0111 remain separate and unchanged.

The user's memory-queue follow-up is implemented, tested and deployed. The earlier
try-lock contention-drop design is superseded by preallocated atomic pool
publication, 256-record batched output and 250 ms routine flush coalescing. The
fixed queue plus writer buffers consume 36,065,472 bytes (34.4 MiB). Ordinary
contention cannot discard a record; actual pool exhaustion rejects/counts a new
record while accepted records retain ownership until drained. Process/transport
identity and writer bytes/calls/failures/latency are now reported.

Clean pushed deployment integration `e1996d67e42df7ee0ba63e2fc7577a43d132af48`
on `codex/render-jitter-deploy-20261022` preserves the exact installed seven-file
startup/subtitle delta. Stable patch ID versus the feature head is still
`5b5c15e4e0d5cf0fd6bb483eb37b254603e56fde`. The clean full-LTCG x64 Release
rebuild passed (0 errors, 69 warnings), and **1230/1230** relevant native tests
passed. Matching player, standard renderer and genuine Beta renderer are deployed
with sidecars and verified hashes; active configuration is unchanged and Config
remains running. Latest player PID: 55448.

Controlled v142 contention tests retained **32000/32000** records; sparse batching
reduced writer sink calls **150 -> 13**, flushes **27 -> 12**, preserving the same
76800 payload bytes. A production formatter/file-sink run retained all 150 records
at 50 messages/s and emitted about 26.6 KB/s, with zero write failures. New live
startup has no active input: the 124 s idle writer observation has zero reported
losses/errors but does not validate sustained playback or a new fullscreen focus
attempt. Earlier successful fullscreen focus evidence is retained below as
historical evidence on `c3da9ecd`; its focus source is unchanged by this revision.
No causal live jitter A/B improvement is claimed. Human PR review and field
validation remain open.

## Fullscreen readiness addition (2026-10-22)

Prior fullscreen focus work in PR #186 is already merged and installed. The
active `C:\Videoprocessor\vp\logs\vp.log` entry at 22:09:37 records the delayed
focus attempt with `requested=0 acquired=0`. Source review identifies a separate
lifecycle gap: the one-shot five-second activation opportunity can arrive before
the current fullscreen presentation has successfully revealed.

Implemented a pure GUI-owned session helper binding the current revealed
fullscreen HWND and renderer generation. It retains the original five-second
deadline from fullscreen entry. A later validated reveal schedules the remaining
one-shot immediately against that deadline; there is no 250 ms retry loop. The
opportunity is consumed before the native call, including on Windows denial.
Windowed, superseded-generation and shutdown targets cannot activate. The
existing one-shot native foreground API behavior remains; general renderer-
restart focus reclamation and new configurable shortcut policy are outside it.

Ten deterministic tests cover readiness before/after the deadline, stale timers,
HWND/generation changes, repeated events, denial, windowed mode and shutdown.
Independent GUI review found no blocker: reveal scheduling follows shield release
and retarget completion; same-session renderer reconstruction retains the attempt
state, while true exit/shutdown cancels it. Readiness guards avoid transient flags
that could clear without another reveal and strand the opportunity. These tests
establish session logic; the recorded live startup separately establishes one
successful Windows foreground grant.

[VP-0111](../backlog/VP-0111_configurable-foreground-shortcuts-and-focus-restoration.md)
remains Backlog for the broader configurable shortcut/focus-return behavior.
[VP-0118](../done/VP-0118_make-alt-enter-enter-fullscreen-from-inactive-startup-request.md)
remains Done for fullscreen toggle state and Right Alt normalization.

## Authorized deployment and live fullscreen verification

Final integration evidence is under
`E:\codex\videoprocessor\render-jitter-deploy-20261022\artifacts\vp-0204`:
`validation-summary.json`, `deployment-receipt.json`,
`deployment-transcript.txt`, `release-deployment.log`, and
`test-results/deployment-native.trx`. The receipt pins both feature and deployed
source, build mode, process arguments, previous/new PID, backup and hashes.

- Full solution x64 Release: passed, 0 errors and 69 warnings; warning provenance
  has not been independently classified as baseline-only.
- Native deployment suite: 1,225 executed, 1,225 passed, zero failed.
- Focused Config checks: `warm reveal grants foreground permission` and
  `normal window architecture has no lease polling`, both passed.
- Backup: `C:\Videoprocessor\vp\backup-vp0204-c3da9ecd-20261022`, containing the
  three binaries/sidecars plus configuration, state and log evidence.
- Old PID 42236 closed gracefully without forced termination; PID 5800 started
  with the same elevated launch intent, VP Renderer (Beta), fullscreen true,
  windowed-fullscreen false, noui false and start-minimized true. The existing
  Config process (PID 73936) remained alive.
- Active `VideoProcessor.cfg` SHA-256 before and after is identical:
  `DE93ABA8E976F5F90C25A0CCE3891DD9803F60817F719ABEBF72B9C02E531CB3`.

The three deployed binaries match the built Release artifacts:

| Deployed file | SHA-256 |
| --- | --- |
| `VideoProcessor.exe` | `4B6C15D7727096412C366A95A6D9EC273C6678138B02149952A5E99C404010DF` |
| `vprenderer/VideoProcessorVPRenderer.dll` | `1C1163B4075BE8B3F8EB8BE9A57362BE349499EB62A0962E2A28DE25D0D01BB5` |
| `vprenderer-beta/VideoProcessorVPRendererBeta.dll` | `1272C7B16D5800A20DA3FE2148AC124477CB3C19CEE6F5AA50C3E49102F69DF8` |

`C:\Videoprocessor\vp\logs\vp.log` records clean host/renderer build identities
at 23:13:25/23:13:32. At 23:13:35, generation 1 revealed target
`0000000010C7009C` with `fullscreen_intent=1` and swapchain-submitted evidence.
At 23:13:37 the matching delayed activation logged `requested=1 acquired=1`,
foreground and keyboard focus both equal to that target, and `overdue_ms=15`.
This confirms one real activation after reveal; it is not a guarantee that
Windows grants every future focus request or evidence of reduced playback jitter.

## Passive post-deployment observation and Review follow-up

Read-only aggregation of the installed `c3da9ecd` session covers 2026-10-22
23:13:50-23:15:50 local time, with 60 observed performance windows / 120.062
reported seconds / 7,197 submissions. Artifacts beside the deployment receipt:
`live-postdeploy-summary.json`, `live-postdeploy-analysis.txt`,
`live-postdeploy-window.log`, and `live-postdeploy-aggregate.py`.

- Active work wall mean/peak: 4.173/8.428 ms; preparation 3.736/5.785 ms;
  render 0.295/3.486 ms; swap 6.678/7.413 ms, including intentional pacing.
  These are wall durations, not render-thread CPU execution times.
- Zero reported deadline/source/display-period overruns, renderer/capacity/
  cadence drops, cadence repeats or backlog-recovery events. Worker submitted/
  completed 7,197/7,197, with zero failed/cancelled/contended/full and queue
  snapshot maximum zero. Preview completed 7,196 of 7,197 observed attempts.
- Exact worker measurement imports: 6,941 with 16 rejects; render-side global,
  picture and darkness each report 5,636 hits / 5,646 requests / 10 scans.
  Worker scan work still consumes resources outside these render-side counters.
- Process CPU mean was 3.718% of the 16-logical-processor machine, with maximum
  reported sample 4.880%. This is whole-process, whole-machine-normalized CPU.
- NVIDIA telemetry reports zero actual driver calls in this SDR session;
  real-driver slow-call handling has not been validated by this observation.

**Review follow-up before merge: logger diagnostic loss.** During the same
120-second reporting interval, endpoint deltas of the observed reported writer
stream show 3,163 accepted/processed records and 459 producer-lock contention
drops: 12.673% of that stream's attempted records, with zero queue-full drops
and zero truncation. All 25 reports increase monotonically, without observed
reset/interleaving. The prefix lacks a module/transport identity, so this is not
proven to be an application-global loss rate. The intentional nonblocking admission
protects producers but demonstrably loses diagnostic evidence in normal playback.
Review that retention/contention tradeoff and add attributable transport identity
before merging PR #192; do not describe the transport as lossless or infer
completeness from an empty queue. Mean flush was 48.975 microseconds and lifetime
peak was 274 microseconds in these reports.

These are observed-record aggregates; missing diagnostics are not reconstructed.
The input was not a controlled, content-matched replay against the older build.
This interval establishes ongoing playback and active counters, not a causal
improvement in jitter or the absence of every unlogged event.

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
| Pre-focus measurement-source full solution x64 Release build | Source `3f5dd8a1`; passed, 0 errors, 0 warnings | `measurements/release-3f5dd8a1.log` |
| Pre-focus native regression suite after direct-sampler correction | 1,023/1,023 passed | `measurements/results/dense-fix-native.trx`, `measurements/dense-fix-native.log` |
| Initial focused native suite | 1,021/1,021 passed | `native/focused.trx`, `native-tests.log` |
| Isolated final measurement DLL | Passed | `measurements/analysis-final-run.log` |
| Initial paired 4K fixture test | 1/1 passed; all evidence parity checks passed | `native/paired-4k.trx`, `paired-4k.log`, `paired-4k-summary.json` |
| Prior Windows Qt Config suite; UI unchanged | 104 passed, 4 failed out of 108 | `config-tests.log` |
| Historical pre-focus source/validation manifest | Exact source `3f5dd8a1b2693e79645bab4dfdf24e76fba53cc8`, native 1,023/1,023 and isolated 1/1; final integration manifest is above | `validation-summary.json` |
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
runtime comparison. Those pre-focus measurements preceded the separately recorded
authorized deployment above; active configuration remains unchanged.

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
- The user authorized build and deployment after the fullscreen addition was
  validated. Paired Release build/hash/backup and restart evidence is recorded
  above; active configuration/LUT edits remain outside this addition. Automated
  tests alone do not establish live playback timing acceptance.

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


Fullscreen follow-up reactivation audit after fetching origin/main
`f31313a5398442f1d399d586087316751745dc3b` found the same 223 canonical files/index
rows, 202 roots and 21 children, with registry Last VP-0204 / Next VP-0205 / count
223. No duplicate, missing, orphan, child-order or state discrepancy was found.
Known VP-0070/VP-0088/VP-0168 formatting exceptions remain untouched. VP-0204's
file, exact In Progress status and index row move together; VP-0203 is unchanged.


Post-deployment Review-transition audit after fetching origin/main
`1492c875b25067f7fc9e49ee95a9e12a4e7bb4da` found 223 canonical files/index rows,
202 roots and 21 children with registry Last VP-0204 / Next VP-0205 / count 223.
No duplicate, missing, orphan, child-order or state discrepancy was found.
Known VP-0070/VP-0088/VP-0168 status formatting remains untouched. VP-0204's
file, exact Review status and index row move together; VP-0203 and the broader
Backlog VP-0111 remain unchanged. PR #192 remains draft and unmerged.


## Memory-backed logging follow-up (2026-10-22)

Implementation resumed at the user's request after the live admission-loss
measurement. Replace producer try-lock/drop-on-contention with preallocated
bounded record storage and atomic handoff; drain batches and throttle routine
flushes on the writer. Memory budget may increase; real capacity exhaustion must
remain counted. Validate concurrent retention, stalled producer/writer isolation,
shutdown and restart, producer latency, bounded memory and actual write/flush
volume. Update PR #192 and rebuild/deploy its tested Release integration while
preserving the installed startup/subtitle delta and active configuration.


## Memory-queue follow-up validation and deployment (2026-10-23)

- Feature `6e7ce51103269a5fd27ac5adcaa40c3b12a3635a`; deployment
  `e1996d67e42df7ee0ba63e2fc7577a43d132af48`. Both are clean and pushed.
- Native final TRX: 1230 executed/passed, zero failed/skipped. Includes real
  reservation pause, independent producer/consumer progress, close during admitted
  publication, 3200-message no-loss concurrency, 32000-offer recycling/accounting,
  blocked writes/flushes, actual-full rejection, reopen, full-sized production
  byte formatting and partial-batch coalescing. Existing crop/motion/focus suites
  also pass. No claim of a newly repeated full Config suite; prior baseline
  104/108 and two focused Config checks remain historical evidence.
- The preliminary incremental build's LNK1318/LNK1103 compiler PDB failures are
  preserved in `release.log`. The first broad test run exposed a partial-batch
  timing path. It was corrected, then a clean full-LTCG Release rebuild and the
  final complete selected native suite passed. This was investigated and fixed,
  not a blind rerun. Release optimizations remain enabled.
- Matched-toolset x64 v142 /O2 contention harness: ten 3200-record trials with
  eight producers, 8192-slot capacity in both designs. Old try-lock retained
  4983/32000 (27017 contention drops); new pool retained 32000/32000 with zero
  full/contention drops. Median trial p99: old 0.9 us, new 2.6 us; maximum observed
  call: old 11.7 us, new 163.7 us. These wall times include scheduling/competition
  and do not establish a per-thread latency bound. Old admission often returned
  faster because it discarded the record.
- Controlled sparse stream: 150 records / 76800 bytes / about 3 s in both
  transports, all retained; writer sink calls 150 -> 13; flushes 27 -> 12.
  These are sink/API call counts, not physical disk operations.
- Separate production formatter/file sink at 50 messages/s with 511-byte messages:
  all 150 retained over 3.015 s, 13 writes / 12 flushes, 80100 formatted bytes and
  80250 on-disk bytes including Windows newline expansion (~26.6 KB/s). Writer
  work 1467 us total / 545 us peak; flush work 237 us total / 73 us peak; producer
  p99/max including timestamp capture 21.5/45.7 us. Pool high-water 14 and zero
  write failures. Preserving more messages can increase byte volume versus the
  lossy prototype; batching reduces calls, not information volume.
- Deployment completed 2026-10-22 23:59:42 local. The old player was already
  closed cleanly (its log shows system close/normal shutdown at 23:41:15); this
  deployment did not terminate any existing process. New player PID 55448,
  Config PID 73936 retained. Startup intent remains Beta/fullscreen/normal UI,
  minimized controls. Both optional plugins load API 28; host clean source is
  `e1996d67`. Capture input is inactive, so no renderer build-reveal/focus event
  or sustained-playback result is invented for this run.
- Backup:
  `C:\Videoprocessor\vp\backup-vp0204-logs-e1996d67-20261022-235942`.
  Includes all three previous binaries and sidecars, active config, state and
  prior log. No active configuration edits. Before/after config SHA256 remains
  `DE93ABA8E976F5F90C25A0CCE3891DD9803F60817F719ABEBF72B9C02E531CB3`.
- Verified deployed SHA256:
  - host `A9FB14B26D0D1CAB321BBABE06B80EFB31BE430535100B210AA8DE8C83C4C017`
  - standard renderer `C7BAF83BD1A241FC03E0DECED2F640B761ACF1F5C940B61718E4F036FD9C94F6`
  - Beta renderer `F3844E5A569E1A58DBCF125C313FEDED305126148200C3210C0B9B1DB7EEA0E8`
- Idle observation 2026-10-23 00:00:14 -> 00:02:18 (124 s), identified PID 55448 /
  transport 00007FF68DCE3570: 5 admissions/processed, zero full/contention drops,
  truncations or write failures; 29 writes/flushes, 13727 formatted bytes. This
  low-load result is not a playback benchmark. Lifetime pool high-water 63/8192,
  configured buffer budget 36065472 bytes. Writer/flush lifetime peaks 334/96 us.
- All latest receipts, final and failed TRX/build logs, reproducible benchmark
  C++/CSV/toolchain records, production-sink results, raw idle snapshot/identified
  stream and aggregate parser are under
  `E:\codex\videoprocessor\render-jitter-deploy-20261022\artifacts\vp-0204\log-memory\`.
  Start with `validation-summary.json`, `deployment-receipt.json`,
  `benchmark-v142-summary.json`, `production-benchmark.json`, and
  `live-idle-summary.json`. Older parent-folder evidence remains historical.
- The prior contention-loss and untagged-transport review findings are resolved
  in the implementation and deterministic/controlled tests. Active-playback
  follow-up remains appropriate when input is available; do not infer a live
  jitter reduction or real-driver NVAPI-stall qualification from the idle run.
