# VP-0225: Review and optimize subtitle processing to eliminate repeatable playback stalls

## Status

2026-11-03 follow-up (source `7c264419b7e648fa03992ee58512d7de1559a176`, not deployed): the matching x64 Release Native lane passes 1,084/1,084 tests (`20261103-141551-53d4a8dc`). This is not a qualification result. All matching x64 Release host, Standard/Beta renderer and focused test builds passed; the three focused CPU tests passed in run `20261103-141756-16008dbd` (`ValidatedSamplerMatchesCanonicalCodesAcrossFormatsRangesAndCoordinates`, `CurrentCueProofReceiptReadyWhileExternalTraceSinkBlocks`, and `HeldCurrentPreparedCuePreservesOriginalPendingSeedWithoutRenewingItsAge`). This is not GPU or full qualification evidence. A CPU-only static-footprint benchmark on `958c230d43bf0f08352d3760e29513147149dc0a` measures 3.52–3.62 ms before and 2.92–3.02 ms after across three runs of 32 repeats using the validated V210 view; exact phase failures remain recorded. This microbenchmark does not establish end-to-end throughput.

The existing live trial remains source `69c51f18f516866912d89c0900c7da9478ab9bf5`. A short Reconstruction window recorded 240/240 current-selected, submitted, composed and prepared outputs from 14:14:39.623 to 14:14:49.589Z, selected step 1, and maximum submission-log gap 49.6082 ms at 23.976 Hz. Native/source shapes were `[0,272,3840,1888]` and `[22,272,3818,1888]`. Saved state changed to Reconstruction at 14:12:33 without agent input; cause is not inferred. This brief log window is not visual acceptance or full qualification. Per the user's playback report, paused screenshots now look fixed but live playback still often flashes zoom outside the bars and briefly loses subtitles while the OSD AR remains stable; the user says it is not reliable or acceptable. This is the primary unresolved reproduction. The short 240-frame log window does not override that report or establish acceptance. See the [bounded follow-up summary](../assets/VP-0225/followup-7c264419-20261103/summary.md), [manifest and hashes](../assets/VP-0225/followup-7c264419-20261103/manifest.json), [paused Reconstruction window](../assets/VP-0225/followup-7c264419-20261103/live-reconstruction-69-paused-window.json), and [matching focused Release iteration](../assets/VP-0225/followup-7c264419-20261103/focused-3-release-iteration.json).

2026-11-03 current user-authorized live trial: clean x64 Release source `69c51f18f516866912d89c0900c7da9478ab9bf5` was deployed at 13:55:10; PID 63656 responded. Deployment is recorded as `UserAuthorizedLiveTrial`, not qualified. The backup is `C:\Videoprocessor\vp\backups\trial-69c51f18-20261103-135509`. Before restart, `VideoProcessor.cfg` and `.state` matched hashes `ADB4E329...19F3` and `99CF94A6...9222`; after startup the config hash stayed unchanged, while the state changed only `profile.subtitles` from `reconstruction` to `classic` at 13:55:23. The post-start state read Classic; a later state capture at 14:12:33 read Reconstruction. Why these profile changes occurred remains under investigation.

All matching x64 Release builds and the focused 3/3 CPU run passed at `20261103-134055-25e3feb6`. The 1,083/1,083 Native result is from prior source `3ea6335a`, not this trial source. GPU run `20261103-134859-dbbd0ddc` failed all four Standard/Beta actual-renderer cases: both `StaticCueWorkerFitsLiveFrameBudget` worker-proof maxima exceeded the 16.683 ms input period (21.0904/18.4053 ms; mandatory frame work was 12.67/12.37 ms), and both `DecodedDvUnknownInputKeepsCurrentSource` cases reported persistent caption composition flashing off. There is no matched baseline to attribute these results as new or pre-existing. Cold shader preparation totals were 64.79/64.64 seconds; one background-3 pass was 25.32 seconds. The user accepts about 25 seconds for a heavy background temporarily, but the approximately 65-second total remains unresolved. No 3x improvement is claimed. The trial remains unqualified and the story remains Review. See the [69c51f18 trial summary and bounded receipts](../assets/VP-0225/live-trial-69c51f18-20261103/summary.md), [deployment receipt](../assets/VP-0225/live-trial-69c51f18-20261103/deployment.json), [focused build and CPU receipt](../assets/VP-0225/live-trial-69c51f18-20261103/69c51f18-focused-cpu-build.json), and [GPU failure summary](../assets/VP-0225/live-trial-69c51f18-20261103/69c51f18-gpu-failed.json). The 7 MB TRX is not duplicated; its source SHA-256 is in the manifest.

Earlier user-authorized live trial: clean x64 Release source `d8ae41757d186b26dab08276ef334a34325f6cc5` was deployed at 12:44. It was superseded by the 69c51f18 trial above. PID 57168 responded at startup; its backup is `C:\Videoprocessor\vp\backups\trial-d8ae4175-20261103-124413`. Its Standard/Beta actual-renderer run `20261103-123652-71c45a17` failed both worker-proof timing assertions: each processed 302 distinct sources and returned 299 receipts; Standard/Beta grouped 56/55 scans, with main-path maxima 14.7982/12.5986 ms and worker-proof maxima 20.6064/22.9182 ms against a 16.683 ms source period. Only the first 59.94 Hz input-rate case ran. See the [d8 trial summary](../assets/VP-0225/live-trial-d8ae4175-20261103/d8ae4175-summary.md), [deployment receipt](../assets/VP-0225/live-trial-d8ae4175-20261103/deployment.json), [Native receipt](../assets/VP-0225/live-trial-d8ae4175-20261103/b6710bf-native-1083-standard.trx), and [actual-renderer receipt](../assets/VP-0225/live-trial-d8ae4175-20261103/d8ae4175-actual-pair-failed-standard.trx).

Review

2026-11-03 steam-engine cue follow-up (tested code `3ea6335ad5e772505e38878ad7baf91bace38300`; later documentation HEAD `69c51f18f516866912d89c0900c7da9478ab9bf5`, not deployed): the paused source 6562 retains accepted physical bounds `[0,270,3840,1886]` from source 486. Its nearby measured candidate `[0,272,3840,1888]` did not verify a crop axis, so it was not accepted. The current caption extends to y=2022; raw-current presentation expands to y=2026 and is about 2.19:1 while the physical crop remains about 2.38:1. At captured source 6842, cue32 is detected and held but the output is uncomposed and uses the current raw source. This distinguishes presentation fallback from a physical AR change.

The raw scan and tracker state are separate: 718 typed current-cue proof attempts reject as `cadence-current-glyph-changed`, while the adjacent source 717 raw scan is full, independent, and nonheld; the accepted tracker result remains held. The strict glyph comparison reports 100% coverage and zero difference, so the rejection is not evidence of a changed glyph. A CPU replay from a captured V210 frame showed the clean control certificate, a held-control case with an unrelated one-row bar shift (273→274) rejecting before the physical y-ownership join, and the same control accepting after fix `35018ff5`; a negative change outside the cue’s owned pixels still rejects. This is CPU evidence only, not GPU or live-playback validation.

The focused CPU run passed 2/2 on `3ea6335a` (`20261103-133146-f69b91b6`); the Native suite passed 1,083/1,083 (`20261103-133614-d43641aa`). The separate live shader cold-start observation took 71.6 seconds across the requested background 3/unblurred and background 5/blurred variants; the prewarm sweep predates the verified RC6 package source. Shader change `243f1b47` remains untested on GPU. These results do not qualify either live trial or establish runtime acceptance. Story state remains Review. See the [paused-cue and replay evidence](../assets/VP-0225/cue-timing-review-20261103/steam-engine-20261103/evidence.md) and its [hash manifest](../assets/VP-0225/cue-timing-review-20261103/steam-engine-20261103/manifest.json).

2026-11-03 worker/cadence follow-up (clean source `9cd1fe9b6785106f2b3fe92f1aaaf2d5a1bbec6b`, not deployed): the Native TRX ran 1,081 tests, with 1,079 passing and two new semantic-refresh failures: `CurrentCueWorkerBurstsScansAfterFreshInsidePictureLineChange` and `CurrentCueWorkerRunsSemanticRefreshAtBoundedInputCadence`. Their verbatim TRX and run summary are retained in [the Native receipt](../assets/VP-0225/cue-timing-review-20261103/9cd1fe9b-native-1081-standard.trx) and [failed-run JSON](../assets/VP-0225/cue-timing-review-20261103/9cd1fe9b-native-1081-failed.json).

The same source's focused actual-renderer run executes seven tests: three pass and four fail. Both Standard and Beta `StaticCueWorkerFitsLiveFrameBudget` cases and both `DecodedDvUnknownInputKeepsCurrentSource` cases fail. The focused run reports zero worker proof jobs; the source-generation mismatch is a hypothesis under investigation, not a confirmed cause. This is partial actual-renderer evidence, not full GPU or motion qualification; worker integration remains unverified. No failure was waived. See the [focused TRX](../assets/VP-0225/cue-timing-review-20261103/9cd1fe9b-focused-actual-standard.trx) and [run JSON](../assets/VP-0225/cue-timing-review-20261103/9cd1fe9b-focused-actual-failed.json).

Earlier focused evidence at `c757a38f4ca591e6afcfc8f2523eeb18a559ec5c` shows all six persistent-caption fixtures passing at 23.976, 24, 59.94 and 60 source Hz with observed queue target 4 and hard capacity 9. The run as a whole is not a qualification: 9 of 12 tests pass and three UNKNOWN/current-source tests fail. The [TRX](../assets/VP-0225/cue-timing-review-20261103/c757a38f-focused-actual-standard.trx) and [run JSON](../assets/VP-0225/cue-timing-review-20261103/c757a38f-focused-actual-failed.json) retain that distinction. Full actual-renderer/GPU qualification and live acceptance remain open; last deployment remains `53058498`.

Release provenance is also corrected: receipts identify beta RC6 `7af23836ae937353edb50a5b1b3f8e9176634480`, the republished RC6 build `d5607e080537ad2713e8e746b78226dda44162cb` with application sources `c716bd8108b03b0ef279c14c584e097eecc29515`, Magewell RC6 `5b8132b6f2a3d900d189496dcc86e002c300ae26`, and Stable DeckLink first at `605ec4af39b934ac4bbd0f3e7dbbe7e31bec7e63`, then replaced under the same tag with `a8f62855c246019e3d9262f384e58c96d60f3147`. The current beta baseline `a591ad3b532771911e85f8ef026958279f2065f0` descends from `a8f62855`; the later footprint cadence change `477fdfb6f06f3cacfb00f27d905a39be4a851ce8` is not an ancestor of that beta. The released sources already had nonblocking exact-source worker analysis and a glyph-local pending bridge capped at 32,768 samples. Consult the [replacement record](../assets/releases/1.4-rc6-updater-20261030/README.md) and [historical timing findings](../assets/VP-0225/cue-timing-review-20261103/findings.md).

2026-11-03 current timing review (7e49dec1, not deployed): all 1,073 native
contracts pass. Focused actual-renderer coverage executes 12 tests, one passes
and 11 fail. The desktop is currently 3840x2160 at 24 Hz, independently read
through EnumDisplaySettingsW and Win32_VideoController after the run. The six
persistent DV fixtures feed 59.94 Hz but never supply the host's target-refresh
metadata to the renderer; their 23.94-24.09 distinct submissions/sec cannot be
classified as a subtitle throughput regression without accounting for legitimate
24 Hz presentation. Joins are complete; maximum source age is about 205-207 ms,
and the first/last latency means are nearly flat. Actual target-aware cadence and
bounded source age still need qualification; queues/timeouts will not be enlarged.

Static-cue baseline reuse now has measured actual effects: advanced footprint
setup rearms 46-56 times per case at 0.13-0.25 ms median; generic pending-seed
setup remains 2.32-2.66 ms median in those modes. Classic median ready setup is
0.095-0.162 ms, but current pending-pixel checks can still cost 11.5 ms median
and 20.4 ms peak in one case. These are separate per-call samples, not additive
percentiles or whole-pipeline speedup. Full asynchronous detector grouping still
runs; incremental grouping avoidance remains an explicit optimization candidate.

A confirmed mandatory-path defect remains: missing optional subtitle readiness
invokes PresentInputCadenceRepeat and submits the preceding whole movie frame for
a new input. This conflicts with the live-HDMI contract; removal is in progress,
while explicit cadence conversion remains. A separate UHD pending source preserves
current raw caption pixels but loses an already-ready composition for one frame,
expanding the viewport; its downstream exact prepared-erasure handoff is under
review. One UNKNOWN oracle wrongly required renderer-owned IDs on worker-side
physical measurements; the correction retains exact source/physical/bounds/order
joins. No repair after 53058498 has been deployed. Full qualification, paired
motion GPU tests and live acceptance remain open.

2026-11-03 full runtime follow-up (312c1370, not deployed): all 1,071 native
contracts pass inside the full x64 Release run. Standard executes 1,091 tests:
1,082 pass and nine actual-renderer cases fail; the Beta runtime lane is not reached.
The real UHD single ready-rearm probe takes about 0.09ms with 8,100 mask words and
current glyph/backing checks, but actual 60Hz Classic still rebuilds on 38 of 57
eligible same-cue attempts, leaving median setup near 9.6ms. No whole-pipeline
speedup or passing qualification is claimed. Computed first-rejection diagnostics
are being added before changing any eligibility guard.

Current-source handoff now reuses the exact successfully composed final operation
and consumes the exact-current one-use cleanup receipt. The remaining dense-cue
failure is at unowned movie pixel (2291,1897): the raw detector line bbox extends
to2298 while actual glyph capture/content ends2290. Including raw bbox padding in
the cleanup hull wrongly rejects current movie motion. Independent review confirms
that only prepared shader extraction/content, not raw detector bbox padding, should
bound this projection; narrow repair and regression are in progress. New UNKNOWN
tests require current source pixels rather than replaying an old whole frame.
Shader-startup checks also still reject current raw-union outputs. A prior inference
about translated Classic output after the mode switch was incorrect: those records
precede F6, while the actual pending outputs119-125 are the current250-1956 union. Last deployment remains
53058498. Full qualification, motion GPU coverage and live acceptance remain open.

2026-11-03 native follow-up: clean source 3d7fb6a8 passes all 1,068 native
contracts (run035012-eed9d9c5). The tracker now consumes verified current pixel
continuation without recreating bar authority; it retains original cue/reference
and lease and advances only queue identity. Ordinary and retired-input tests check
that no new accepted-source/cue/bar certificate is exported. The pending footprint
can rearm from a consecutive independently analyzed stride-1 source while retaining
its original pixel baseline; normal stride-2 cadence rules are unchanged. A changed
cleanup sample still rejects after rearm. Independent read-only review found no
critical stale-pixel, authority or lease-renewal leak. Actual runtime costs and
renderer continuity after these changes remain to be measured. No later deployment.

2026-11-03 timing/decision follow-up (clean 4a40caf1, not deployed): the focused
23-case run passes 13 and fails 10. Actual 59.94/60Hz DV stage instrumentation
finds repeated prepared-footprint setup around 5.6-7.1ms p50 and pending-seed setup
around 2.4-2.7ms p50, with individual seed calls exceeding a 16.7ms frame budget.
These are per-call measurements, not additive percentiles or end-to-end latency.
The existing footprint rearm expects a +2 source step even for stride-1 analyzed
frames; a narrower +1 rearm for pending footprints is under review. Normal cadence
must keep its existing stride-2/single-skip rule and immutable baseline.

A confirmed handoff defect explains source304's one-frame composition/framing loss:
exact current glyph and bar verification succeeds, then the footprint preview clears
semantic bar authority; the tracker rejects that successful presentation proof as
bar-evidence-lost. Repair must pass a presentation-only current proof to the tracker,
without inventing bar/native/absence authority or renewing its seed. Source379 is a
separate conservative new-bar-ink rejection and remains unresolved. No test waiver,
full passing qualification, later deployment or live acceptance is claimed.

2026-11-03 current review (clean source fffc06f339a59b8577f714d8adf59be58ccda177,
not deployed): advanced pending reuse now uses the actual successfully composed
extraction/cleanup footprint. Current movie pixels in safe outer cleanup strips
remain current; glyphs/backing and newly visible encoded-bar ink are checked on
the exact current source. A source-bound one-use geometry receipt protects those
strips in cleanup, panel and layer shaders. Pending proof adds no native/bar or
caption-absence authority and does not renew the original pending lease.

All six persistent-caption actual-renderer cases pass on Standard/Beta, each at
59.94 and 60Hz: Classic, generated background and Black. The latest full x64
Release qualifier built successfully but FAILED: standard TRX executes 1,084 tests,
1,070 pass and 14 fail. Four older cue lifetime cases and ten actual-renderer
cases remain under investigation. The beta full lane was not reached. No release
qualification is claimed. Existing strict steady composition and frame identity
checks remain requirements; failures must be resolved before qualified deployment.

The five focused standard GPU tests pass at the same binaries, including protected
cleanup pixels with moved glyphs, exact-current raw output, BOB, cached revision and
old-complete-packet exclusion. This is partial evidence, not full GPU qualification.
The new Off fixtures pass in the full standard run and had passed pixel/framing
checks in both variants earlier. Their actual current-source preempted candidate
route is distinguished from confirmed-FIT recovery; neither fixture is claimed to
reproduce the original recorded live scene. The exact narrower-FIT/raw unit remains.

The earlier 289,920-sample generic UHD probe measured pending p50 8.50-8.67ms and
max 15.36ms, with repeatable seed cost around 10ms. Those exploratory timings are
NOT measurements of the new prepared-footprint runtime. Remaining ready-frame
template setup and mandatory-thread work still need cost attribution; bounded
sample counts alone do not establish 60Hz headroom or whole-pipeline latency.
Last deployed build remains 53058498. No later deployment or live acceptance.

2026-11-03 deep timing review: live HDMI throughput, bounded latency and independent
current-frame presentation are now permanent personal/repository AGENTS.md rules.
Static subtitle cues lasting seconds are explicit context: reuse verified layout
with fresh current-pixel/novelty/backing checks, never old movie/cleanup pixels.

The current-source fallback repair is still under validation and has NOT been
deployed. Last deployment by this chat remains 53058498. Native contracts pass
1,058/1,058 at d3f07cd8. At facc6212, Standard/Beta caption/UI bursts pass startup
and real SDR-to-HDR input transition with 24 consecutive current-source outputs;
worst steady submit gap is 48.9904ms at 23.976 input Hz, mean input-to-submit delay
stays about 127ms without growth on the unchanged four-frame fixture queue. All
four existing 4K V210 Classic/Black UI/return checks pass; two standard-dependency
raw-current GPU/BOB checks pass. These are focused evidence, not full qualification.

The strict persistent-cue tests at c01ba269 correctly block deployment: steady
Standard Black source17 and Beta Reconstruction source20 drop current composition
after the pending verifier samples moving background at (1548,1886), four pixels
outside glyphs beginning x1552. The cue template survives backlog retirement,
but the expanded-ROI exact-color check rejects it. A fresh full scan reassociates
the identical cue on the next source. The fix in progress separates current owned
glyph identity from movie background and adds current-band novelty/backing evidence;
steady/composed/pinned viewport and pixel assertions remain strict. A separate
Classic startup no-seed fallback is being distinguished from an acquired pose.

Review also found that the older unprocessed native-canvas path can still select
an older composed movie packet in temporal mode; a separate exact-current marker
is implemented but untested. Shader-warmup handling is being checked for current
outboard caption preservation. No queues/timeouts were increased, no user config
was edited, and no passing release or live-scene acceptance is claimed.

[Timing/lifetime findings and remote provenance](../assets/VP-0225/cue-timing-review-20261103/findings.md).
Focused receipts and the failing persistent-cue receipt are in that evidence folder.

Blocking playback regressions reported after deployment: Classic stops with a
volume overlay and during dense subtitles; Off repeatedly changes framing.
Current deployed source remains 53058498 and is not qualified. The partial full
run caught a returning-Classic controller-projection failure; GPU qualification
was stopped after that failure to remove contention with user playback. No later
source changes have been deployed.

Logs establish a 5.796-second Classic source-submission gate (138 pending sources)
and 31 Off presentation-rectangle changes while measured picture geometry remains
unchanged. They do not identify a volume event; the later invalid input/capture
transition is separate. Parse all presentation event types: correlation-only parsing
falsely suggests additional G2 gaps. The complete G2 stream remains continuous.
[Corrected incident evidence](../assets/VP-0225/combined-deployment-20261102/incident-playback-evidence.json).

The controller correction ee4167bf passes all 1,054 native contracts. The Off FIT
precedence repair 2057bb08 has a new counterexample; its first native run caught an
incorrect unretained-result assertion in the new test, now corrected but not yet
revalidated. The main repair now being implemented removes the terminal subtitle
wait in favor of an explicit current-raw, pixel-preserving presentation fallback.
It leaves physical/native AR independent, keeps ready processing, and must prevent
old temporal subtitle packets from being replayed over current raw captions.
New policy, temporal and actual-renderer regressions are required before deployment.

2026-11-02: User explicitly requested combining the AR repair and stutter repair,
then asked to deploy first while qualification continues. Clean, pushed source
`53058498acf3462dd698c3b5b4ea1c0467025769` on
`codex/combined-ar-stutter-20261102` starts at freshly fetched remote beta
`a591ad3b` and explicitly merges qualified `2a3af0c2` and `03d6c37c`.
Both repairs are preserved. Current raw occupied-side margins constrain Classic
placement before the AR repair's certified legal engage-pose floor; UNKNOWN
continuation stays unprocessed and gains no new caption/native/fit certificate.
Candidate trace flag 8 and engage-retained flag 16 have distinct meanings.
The native integration regression verifies the stronger raw margin too.

Manual-review deployment completed at 22:19:41, before full combined qualification.
The x64 Release host and both renderers built successfully. All 28 runtime file
hashes match the combined build; both renderer modules load from the deployment
directory. Restarted process 38028 produces current-source submissions. Settings
were backed up and preserved byte-for-byte during copying; no configuration edits.
Backup: `C:\Videoprocessor\vp\backups\combined-ar-stutter-53058498-20261102-221941`.
Full combined qualification failed; motion suites have not run for this source.
This is not yet a fully qualified deployment or original-scene acceptance.

The earlier stutter-only build `2a3af0c2` passed 1,050 native contracts, 18 standard
and 18 beta actual-renderer checks, and 220 motion checks per dependency (1,526
passing executions). It was deployed and verified, then superseded by the AR chat's
`03d6c37c`; neither separate receipt qualifies the new combined source.

[Combined deployment](../assets/VP-0225/combined-deployment-20261102/deployment-combined.json),
[live startup verification](../assets/VP-0225/combined-deployment-20261102/live-startup-verification.json),
[earlier qualification](../assets/VP-0225/qualification-20261102/validation.json), and
[prior investigation/status history](../assets/VP-0225/combined-deployment-20261102/prior-status-history.md).

Remaining acceptance: complete combined qualification/motion suites; repeatedly
replay the original scene and surrounding scenes under matched settings, verify
picture/caption preservation, and collect matched source-gap and processing costs.
The recording identifies original source `ad9e7701`; it is desktop output and no
exact native capture exists for sources 5671/5672. The new 640px fixture observed
one UNKNOWN candidate and zero eligible raw/no-candidate rows; native raw-only
cases and broader 4K checks are separate evidence. Keep Review until live acceptance.
No beta merge or public release was performed.

## Problem and objective

The same scene stutters severely on every replay while surrounding scenes play
normally. Review the subtitle analysis, readiness, canvas admission, AR
interaction, and presentation path and remove their adverse impact on picture
cadence. Older remote versions that the user reports did not have this issue
may provide useful design or diagnostic evidence; historical comparison or
bisection is optional, not a required implementation method. Preserve correct subtitle behavior, source-derived picture detection,
and current picture/caption safety rather than trading stutter for lost content.

## Reproduction and incident evidence

- User recording: `C:\Users\bslac\Videos\2026-11-02 18-55-54.mp4`;
  19.983 seconds, 1199 frames, 2560x1440, recorded at 60 fps.
- At recording 8 seconds, the drawn correlation timecode is
  `23:56:02.950Z A3E5EE18 G2 O2400 S5579`. The main logged episode at
  18:56:06.660-18:56:08.375 local is approximately recording 11.7-13.4 seconds.
  Use the drawn timecode for precise alignment, not the filename alone.
- The recording is a desktop capture of VP playback, not the original HDMI
  source pixels. It supports pacing diagnosis but is not an exact native-frame
  detector/cleanup fixture. Capture source frames separately if required.
- Incident configuration: 3840x2160 23.976 fps, DeckLink Quad HDMI Recorder (2),
  v210, SDR REC.709; VP Renderer windowed on approximately 59.95 Hz display;
  Standard Scope, Scope zoom/viewport, Classic subtitles, NLS off, motion off.
  Queue capacity 10, observed depth 3. Runtime title identifies
  `codex/dark-scope-relative-edges-20261102`; recover exact host/renderer source
  identities before comparing builds rather than assuming title equals commit.
- Matching `vp.log` shows captured delivery approximately 23.976 fps with
  `skipped_input_frames=0` and `total_missed=0`. GPU frame times are approximately
  4 ms against a source frame period of 41.708 ms. Those counters do not prove
  smooth presentation.
- Submitted sources advance `5668 -> 5673 -> 5677 -> 5681 -> 5685 -> 5689 ->
  5693 -> 5697`, with repeated gaps around 167 ms (roughly 6 picture updates/s),
  an initial 212.756 ms gap, and an earlier 289.054 ms gap at 18:55:59.
- Missing sources 5670-5672 explicitly log subtitle canvas admission `ready=0`,
  `last_submitted_source=5668`, and reason
  `subtitle inspection pending; keep the last complete movie canvas`.
  Presentation skips include 26 in a 3297 ms window ending 18:56:09, classified
  `unclassified-no-submit`; the recorded renderer-drop counter stays zero.
- This implicates readiness/submission gating. It does not yet prove which
  function, worker scheduling decision, or historical change caused the stall.
  Pacing trace callback gaps are between submitted-source records; do not infer
  physical DeckLink delivery stopped from those gaps when delivery telemetry
  independently shows no misses.

Versioned evidence: [findings](../assets/VP-0225/incident-20261102/findings.txt),
[matching log window](../assets/VP-0225/incident-20261102/vp-log-window.txt),
[configuration](../assets/VP-0225/incident-20261102/VideoProcessor.cfg),
[state](../assets/VP-0225/incident-20261102/VideoProcessor.state), and
[artifact hashes](../assets/VP-0225/incident-20261102/manifest.json).
Configuration/state are investigation-time snapshots; the recording establishes
incident selections. The original recording stays at its user-provided path.

## Review scope and optional historical reference

1. Discover the current beta integration branch and tip on
   `billslack2/videoprocessor`, fetch it, and use a clean source worktree under
   `E:\codex\videoprocessor`. Remote `main` is the story base, not the source
   integration base. Preserve concurrent source work and the deployed setup.
2. Another agent is already investigating related AR behavior where subtitles
   stalled the entire renderer. Treat this as potentially the same underlying
   issue. Before implementation, identify that work and its current findings,
   agree on scope/ownership, and reuse shared reproductions or fixes. Do not
   start competing changes to the same pipeline or treat subtitle and AR stalls
   as proven independent bugs. If that work resolves this incident, validate it
   against this story and record the shared resolution rather than forcing a
   separate optimization. Older remote branches/tags/commits are an optional
   reference if useful; record exact revisions and comparable conditions when
   drawing conclusions from them.
3. Trace capture -> analysis jobs -> conversion/preparation -> subtitle result
   publication -> native/caption canvas admission -> submission/presentation.
   Explain every omitted source in the incident, distinguishing deliberate
   admission suppression, worker backlog, stale/mismatched results, blocking
   waits, missed deadlines, render load, and actual capture loss.
4. Measure full/incremental scans, repeat/duplicate work, queue contention,
   locks, cancellation, copy/upload/readback costs, geometry invalidation, and
   whether subtitle work unnecessarily re-runs for each output or holds the
   picture path while a bounded asynchronous result is pending.
5. Produce a review with concrete code references, any relevant historical evidence,
   measured bottlenecks, proposed optimizations, and correctness implications.
   Keep changes focused; coordinate shared ownership with VP-0207 and VP-0214.

## Optimization requirements

- Resolve the cause of picture stalls while subtitle/AR work is pending. A
  bounded fallback may be appropriate, but choose the approach from code review,
  measured evidence, and coordination with the AR work; no particular pipeline
  redesign is prescribed. Any retained layout or pending result must not cause
  prolonged old-picture holds. Handle transitions and new captions safely.
- Keep expensive work bounded and off the critical presentation path where
  practical. Prefer eliminating redundant work and safe result reuse over
  increasing latency, queue size, timeouts, or weakening detection thresholds.
- Preserve frame/cue/generation/format identity and reject stale work after
  seek, source change, restart, profile switch, and geometry changes. Reusing
  layout is not permission to reuse stale erasure/composition or caption facts.
- Preserve picture and caption pixels, correct Classic/generated-gray/Black/Off
  behavior, backing cleanup boundaries, cue acquisition/release, and subtitle
  placement. Do not simply bypass admission guards, disable subtitles, or crop
  away unverified content to meet cadence.
- Derive active boundaries and aspect from source pixels, independently of the
  2.35:1 screen, CIH, zoom, fitting, fill limits, and NLS. No aspect snapping or
  detector-threshold shortcuts as a performance workaround.
- Make intentional suppressed submissions and their causes observable. Zero
  renderer drops must not conceal the measured playback stall.

## Acceptance and validation

- Reproduce and explain this exact incident, including the repeated one-in-four
  submissions, then replay the original scene repeatedly before and after the
  change under matched conditions. Validate surrounding normal scenes too.
- Record matched before/after source-to-submit traces and available
  actual presentation evidence, frame IDs, gap distributions (median/p95/p99/max),
  suppressed submissions and reasons, CPU/GPU time, worker backlog and latency.
  Clearly distinguish submitted from displayed frames when presentation timing
  is unavailable. Account separately for normal 23.976-to-59.95 display cadence.
- Demonstrate no subtitle-induced multi-frame submission stalls, recurring
  approximately 167 ms cadence, or 200-289 ms holds in the reproduced scene.
  Delayed optional subtitle work must not suppress otherwise eligible source
  submissions. User playback acceptance is required to close the reported bug.
- Establish measured before/after subtitle costs and a bounded scheduling budget
  at 23.976/24, 25/30, and 50/59.94/60 fps on supported representative paths.
  Verify the fix does not merely defer work, add latency, or increase backlog.
- Add regressions exercising real frame progression with delayed, missing,
  incomplete, stale and out-of-order subtitle results; pending canvas admission;
  cue appearance/disappearance; geometry changes; and restart/seek invalidation.
  Include actual pixel checks for picture/caption preservation and relevant
  existing detector, backing cleanup, composition, and temporal tests.
- Test Classic, generated-gray/reconstruction, Black and Off; auto/fixed zoom;
  subtitle/picture-boundary crossings; bright/dark scenes and overlays. Preserve
  supported native YUV and RGB behavior, using focused format coverage.
- Complete an x64 Release build and relevant regression checks. Review code and
  measured evidence before acceptance. Any later authorized deployment must
  use the matching Release host/renderer pair and preserve user configuration.

## Deliverables and next action

Deliver review findings, the focused optimization or linked shared AR fix,
regression tests, before/after timing evidence, and live acceptance result.
Include historical comparisons only if useful to the investigation. First
identify the active AR investigation and align ownership/findings, recover the
incident binary identity, and inspect the current remote beta. Record readiness
and the implementation worktree before moving this story to In Progress.

## Tracker audit at creation

Remote main at creation was `cc6fc8e7`. The ID audit found 242 canonical items
and 242 index rows with no duplicate or missing IDs; VP-0225 is the next root.
Existing tracker inconsistencies were observed and left unchanged: VP-0211 is
in `in-progress` with status In Progress but indexed Review; VP-0088/VP-0168 have
legacy status capitalization (VP-0168 also in its index row); VP-0070 does not
have a parsable standard Status heading. Numerous older records append prose
to their status line. These are unrelated tracker-repair work; this new record
uses exact Backlog folder/status/index agreement.
