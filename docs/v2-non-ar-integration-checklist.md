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

