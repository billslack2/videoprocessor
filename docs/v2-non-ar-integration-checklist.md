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
