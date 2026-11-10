# 2.0 beta non-AR integration checklist

Baseline: v2.0.0-beta at 4f687eb07ae358e51bacfd33712b66bec5078da0. Source of selected updates: v1.4.02-beta at a591ad3b532771911e85f8ef026958279f2065f0. Work is isolated on codex/v2-non-ar-integration-20261109.

## Selected first group

- [ ] Fullscreen entry/focus retry (2d9b23e6, 570cb010): applied; x64 Release build and focused fullscreen tests passed. Check no unexpected focus stealing during live playback.
- [ ] Configuration editor: shared profile indicators (e94f444f) and background tone chart loading (2377e898): applied; x64 Release build, targeted profile-status and subtitle-profile tests passed. Shared group definitions include subtitle, viewport and zoom profiles; verify their live selection and activation behavior.
- [ ] Beta VP Renderer libplacebo 7.388 upgrade (93c5009b): applied; x64 Release build and both DLL load orders passed. It changes a shared renderer dependency: verify subtitle composition, AR behavior and presentation cadence on actual hardware before release.

## Subsequent work requiring careful review

- [ ] DeckLink RGB8 Dolby Vision tunnel support: capture formatter, range, shared source interpretation and renderer ABI changes touch detector-adjacent files. Keep existing detector and subtitle decisions; verify r210 as well as RGB8.
- [ ] DeckLink delivery statistics and optional Vertex2 EDID: keep stats distinct from renderer drops; EDID is a separate optional file and must never be applied automatically.
- [ ] Magewell native P210/P010, RGB capture/packing and watchdog: the upstream commits touch active-picture sampling and subtitle-plane disambiguation. Review each hunk and preserve current detector and caption handling; require hardware verification when a card is available.
- [ ] NLS pipeline reuse and madVR source-crop feedback: renderer and geometry-adjacent changes; review against current picture framing and subtitle placement before selecting.
- [ ] Beta updater stable-release handling and packaging: review installer receipt, runtime requirements and current release configuration; do not replace user config.
- [ ] Other later non-AR changes and dependencies: audit against both branch tips; document omissions explicitly.

## Guardrails and validation

- [ ] Review final diff: no imported subtitle/AR detection or presentation policy changes. Shared format and profile plumbing requires explicit review.
- [x] For the first group, x64 Release solution build succeeded; 472 focused native tests and four editor tests passed; both renderer load orders passed. Further groups require their own checks.
- [ ] Verify continuous live HDMI playback, current-source frame cadence, subtitle stability, picture geometry and transitions with the user's DeckLink before any release.
- [ ] No push, merge into the default branch or deployment has been performed.


## Review of remaining items 9 through 4

Reviewed in detached worktree E:\codex\videoprocessor\v2-capture-merge-review-20261109, starting from integration commit 2f75444d. This is a merge simulation only; none of these capture commits has been added to the integration branch.

- RGB8 Dolby Vision (815020da, 17c26a65, 7e746c74), P210/P010 and plane views (5dfb96dc, fc29ada9, 0a7ae616), and prerequisite Magewell telemetry (552e35f3, 9a6b0211) cherry-picked in dependency order without textual conflicts. ActivePictureEvidence.cpp gained only P010/P210 encoding cases; ActivePictureTransitionModel.cpp, SubtitleAspectRetention.h, and AlphaSourceCropPolicy.cpp did not change. RGB tunnel range interpretation and shared renderer/input paths did change and require real-source verification.
- Magewell RGB24 chain (54ce8ca8, 4feda1a7, 193c1a1c): one content conflict in StatsOverlayLayoutTests.cpp. For simulation only the 2.0 side of that test was selected; the final integration must combine the relevant assertions from both sides and re-run tests. Remaining commits applied without conflicts.
- DeckLink capture statistics (05b03f1d) applied without conflict after prerequisite Magewell/shared telemetry. Review callback mutex cost and timestamp correctness on actual DeckLink.
- Magewell watchdog recovery (e656dde1, 955a8d54) applied without conflict after the capture chain. Validate latency, callback/worker lifetimes and device recovery with a card.
- madVR NLS (a5769e53) passed a direct patch applicability check; it was not included in the build simulation. It changes NLS crop geometry authority and removes a full-raster fallback, so it is geometry behavior that needs explicit product review and madVR playback testing.
- The capture simulation x64 Release solution build succeeded and 354 focused capture-format, telemetry, AR and subtitle tests passed. This is not a final integration verification: one OSD test conflict was temporarily resolved in favor of 2.0, tests use mocks/fixtures, and DeckLink and Magewell hardware playback/cadence were not measured.


## Local test build: items 1–6

- [x] Updater beta/stable policy, optional manual Vertex2 EDID, NLS pipeline reuse, DeckLink delivery statistics and Magewell stall recovery selectively integrated. Shared telemetry was adapted without importing newer Magewell format/packing features.
- [x] madVR-only NLS crop feedback integrated. It uses madVR videoCropRect and intentionally enters NLS waiting/native passthrough if feedback is unavailable, instead of full-raster fallback. Live madVR validation remains open.
- [x] Source commit 549b665f: full x64 Release rebuild passed; 479 focused native tests and 37 updater checks passed; both renderer variants loaded successfully.
- [ ] Full interactive Config suite: focus/scroll and slider timing failures occurred while other heavy tests ran; the suite was stopped to avoid interfering with live playback. Needs quiet isolated reruns.
- [ ] Dual-library GPU/motion run interrupted during standard tests to avoid competing with live playback; beta not run. This is incomplete, not a test assertion failure or a passing result.
- [ ] Live capture, source cadence and frame-budget validation on DeckLink; repeat with Magewell after swapping cards. Magewell recovery is only proven by mocks so far.
- [x] Separate test folder C:\Videoprocessor\vp\v2-merge-test-20261110 staged from Release build with the prior test configuration; existing installations/configurations preserved.
- [ ] Merge or push to remote beta remains outstanding.


## RGB8 Dolby Vision integration: item 8

- [x] Original byte-preserving RGB8 tunnel, renderer ABI and Magewell BGRA fixes selectively integrated on codex/v2-rgb8-dv-integration-20261110, based on the 97e2dc66 integration. Magewell watchdog and raw capture transitions were reconciled and exercised together.
- [x] Decoded 12-bit DV analysis, full/limited SDR fallback, actual expected bars and subtitle detection agree across r210/ARGB/BGRA; a saved LOTR decoded frame also agrees across layouts and the subtitle is acquired. That replay reuses already-decoded pixels and is not an actual RGB8 card capture.
- [x] Correct a shadow-only color diagnostic that incorrectly classified decoded 12-bit DV as 8-bit precision based on its RGB8 carrier. No change to active bar thresholds or aspect/subtitle transition policy.
- [x] A dark-bar encoded fixture found a pre-existing r210 startup ambiguity: two alignments can produce a CRC-valid header while decoding different pixels. On each source frame, when distributed frame samples uniquely support one packing, prioritize its probe; actual packet CRC and metadata validation remain mandatory. Retained DV metadata reports the current verified alignment. Controlled tests cover both r210 alignments and RGB8 packed formats.
- [ ] Remaining alignment limitation: if distributed pixels and CRC permit both r210 layouts, this bounded change retains legacy probe order and does not prove the correct alignment. Further investigation is needed before claiming universal correctness; physical transport and timing must be validated.
- [x] x64 Release solution built and the final focused run passed 541 native/renderer/capture/subtitle tests, including an actual standard-renderer RGB8 decoding check. Local DeckLink and Magewell hardware, end-to-end frame deadline and actual RGB8 transport byte preservation remain unverified.
- [ ] Complete standard and beta subtitle/motion GPU regression; run real capture and presentation/cadence with appropriate cards before release. The isolated UHD decoder-only timing averaged 2.69 ms r210 and 2.41 ms BGRA with warm maxima 2.89/2.56 ms; these figures exclude capture, analysis, GPU and presentation.
- [ ] Earlier Config editor focus and timing failures from the 1–6 integration remain unresolved. This feature has not been merged or deployed.

- [x] Beta libplacebo-388: 12 selected tests passed, including real-renderer RGB8 decode, GPU mixer/upload and subtitle-packet reuse. Both renderer DLLs loaded together with API 31 and their intended dependencies.
- [ ] Combined standard and beta subtitle/motion scripts stalled and were interrupted. Individually bounded standard ReconstructionMotionTests, SubtitleCutPasteShaderTests and MotionRendererTests exceeded 45 seconds. Representative Reconstruction and shader tests also timed out at 20 seconds on the unchanged prior integration build and this candidate. This is unresolved, not a passing suite or proof of no regression. Other isolated standard classes passed: source-upload 5, temporal-cleanup 1, subtitle-placement 89 and renderer-dependency 1.
