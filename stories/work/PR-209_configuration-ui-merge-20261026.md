# PR #209: accepted Configuration UI merge

User accepted the deployed UI and authorized merging on 2026-10-26.
[PR #209](https://github.com/billslack2/videoprocessor/pull/209) is merged into
`v1.4.02-beta` at `53f7e234d7b4a607322e03295ce1cac8b9bbb6c4`.
The reviewed feature head was `a35d94ccd588bda366f7fbbe315660396e445785`;
the integration base remained `62700c3b122a0d9723651471f4cae16a6a11efbf`.
The guarded vp-release Prepare/Merge workflow verified both pins and the PR
identity. GitHub listed no status checks; the merge did not bypass any checks.

The accepted changes are the original Option 02 Signal duotone icon family,
shorter tone section headings, Show/Hide Tone Charts captions, unclipped Reset
buttons, a 1120-logical-pixel initial window, and application-wide protection
against wheel edits to dropdowns, numeric controls and sliders. Open popup
lists and scrollbars still scroll, and deliberate keyboard/click input works.

The exact clean feature head built successfully as x64 Release for Config and
ConfigTests. All five focused checks passed: global wheel protection (51
closed dropdowns, 12 numeric controls, later dialogs, popup-list scrolling and
keyboard selection), slider wheel forwarding, tone-control round trip,
responsive layouts, and DPI/accessibility. The UI source is byte-identical to
the accepted test deployment's Config and ConfigTests directories.

Known baseline limitations remain documented: an old discovery-click test
names a removed display control, and the existing drag-timing threshold also
fails on the earlier executable. Neither was represented as a passing check.

The active accepted test deployment remains
`d4d491feca88f38cf66e9f3cccbba7f9fd542c62`, which also preserves the separate
installed DV diagnostics; those diagnostics are not part of PR #209. This
merge did not replace running files or edit active configuration. The latest
prior deployment backup and hash receipt remain at
`C:\Videoprocessor\vp\backup-before-config-wheel-guard-20261026-153704`.

[Structured merge evidence](../assets/PR-209/merge-evidence-20261026.json)
records source pins, local artifact hashes, test names, and deployment receipt.
The adjacent build log is the actual exact-head x64 Release build output.
This is a PR acceptance note, with no canonical story-state transition.
