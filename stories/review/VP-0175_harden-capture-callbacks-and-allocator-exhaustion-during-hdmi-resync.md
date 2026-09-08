# VP-0175: Harden capture callbacks and allocator exhaustion during HDMI resync

## Status

Review (2026-09-08). Created directly in Review at the user's request after
implementation and validation. PR https://github.com/billslack2/videoprocessor/pull/83
merged into `v1.3.005-beta` at the user's request on 2026-09-08 17:18:13 UTC.
Merge commit: `ee9b3f73ad13d225848e0cf37bc8b7b9a36896df`.
Implementation commit: `13735972609cd8d690315aa0dd100499c3f7dd1e`.
Verified integration base: `38e7508f4d2dc66325a3846cbfb8c75d588c1dc8`.
Branch: `codex/harden-episode-resync`.
Worktree: `E:\codex\videoprocessor\harden-episode-resync`.

x64 Release solution build passed with zero errors. All 1,106 native tests
passed, including four new hardening tests. Final code review found no merge
blockers; GitHub reported a clean merge and no status checks were configured
on this PR. No deployment was requested or performed. Remains in Review for
the affected user's hardware validation; the exact original crash cause is
not established.

## User story

As a user watching a series through Apple TV and madVR, I want VideoProcessor
to survive automatic episode transitions and HDMI resyncs without closing or
holding capture buffers indefinitely.

## Incident evidence

- Another user reported Disney+ automatically advancing between Ahsoka episodes
  on an Apple TV 4K 128 GB, using madVR. No manual input, refresh-rate, playback,
  or VP setting change was made.
- Reported build: `v1.3.005-beta`, commit
  `e5f80f89a564a7600957013d2efcdfa4d7764426`.
- Supplied log: `C:\Users\bslac\Downloads\log (3).txt` on the investigation
  machine. This machine's Windows event logs do not apply to the incident.
- At 12:57:32 the log shows PQ/BT.2020 to SDR/Rec.709, HDMI format resync,
  and a six-frame source gap triggering graph recovery. Reset returned at
  12:57:33, but at 12:57:36 conversion/delivery had not resumed. A second
  format/timing resync and packing change to v210 followed; the log ends
  during invalid-state handling. There is no crash stack or normal shutdown.

## Implemented hardening

- Contain standard and unknown C++ exceptions at DeckLink format, frame,
  profile-changing and notification callback boundaries. Return E_FAIL and
  log the callback/error; even a throwing reporter cannot unwind into the
  vendor thread. Reject a null display-mode argument.
- Preserve the capture input interface on StopStreams, EnableVideoInput or
  StartStreams failure, so subsequent callbacks/cleanup do not dereference
  an interface nulled by that failure path. Log each restart HRESULT.
- Use AM_GBF_NOWAIT for conversion sample allocation. On exhaustion, release
  and drop the capture frame and clear in-flight ownership telemetry instead
  of waiting indefinitely while retaining a DeckLink buffer. Rate-limit
  failure diagnostics to avoid per-frame log flooding.

## Validation

- Release build: `MSBuild VideoProcessor.sln /m /p:Configuration=Release /p:Platform=x64`.
- Native suite: `vstest.console.exe x64\Release\VideoProcessor-Test.dll /Platform:x64`.
- Results: 1,106/1,106 passed; report in the worktree at
  `x64\Release\TestResults\resync-tests.trx`.
- New tests cover callback HRESULT preservation, frame exception containment
  followed by a successful callback, unknown exceptions and failing error
  reporters, and real CMemAllocator exhaustion/release/recovery/decommit.
- The tested implementation's beta base was still current at merge review.

## Remaining acceptance criteria

- [ ] Repeated Disney+ automatic episode transitions on the affected Apple TV,
  capture card and madVR setup remain responsive and resume video.
- [ ] Normal sustained madVR playback does not develop allocator-drop bursts
  or a cadence regression; inspect the new allocator diagnostics.
- [ ] Shutdown and subsequent resync remain responsive after capture interruption.
- [ ] Review the new restart HRESULT and callback diagnostics if recovery fails.

An exhausted allocator now permits frame drops rather than indefinite waiting.
C++ exception containment does not recover access violations, memory corruption,
or a driver that hangs inside a COM call. Unit tests do not reproduce the
reported physical HDMI sequence or prove its process-termination cause.

## Related work

VP-0170 covers EOTF transition stabilization; VP-0061 covers DirectShow in-place
reset priming. This story adds failure containment and allocator liveness,
without claiming completion of either related story.
