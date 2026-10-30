# Stable RC6 Magewell refresh — 2026-10-29

[Release](https://github.com/billslack2/videoprocessor/releases/tag/1.4-rc6-magewell-20261029). Source `5b8132b6f2a3d900d189496dcc86e002c300ae26`; merged beta `ad32c3fb05b39df981d3f36a65a61fd857250843`; full source is pinned on `codex/rc6-magewell-release-20261029`. Production application sources match beta; the release integration retains the previously published RC6 packaging fixes and two test-only expectation corrections from `d5607e08`.

Channel `stable`, global sequence `20261030012504`. Existing RC6 release/assets were preserved. Public assets are only the full Setup EXE, portable ZIP and signed `vp-update.json`. Windows binaries/setup are unsigned; metadata signature and exact asset hashes are verified.

Clean x64 Release rebuild; 3538 native tests; 11 Config UI checks; 29 updater-policy and 19 helper-control checks; portable-update/configuration/runtime/installer checks; 326 real-installer checks with an isolated QA identity, including A→B→B→A and repair/data preservation; corrupt-payload failure recovery passed. The real registration and active settings were preserved. Required dual-renderer capture qualification is in the adjacent high-rate-capture evidence directory.

No clean Windows without Visual Studio or new physical display calibration is claimed. Source identity, tool inputs, per-test results, deployment evidence and publication hashes accompany this record. Release notes follow the requested scope.
