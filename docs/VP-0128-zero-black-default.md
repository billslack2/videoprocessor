# VP-0128: zero HDR target-black default

Baseline: latest GitHub default/beta `v1.3.005-beta`,
`b3c0b3a6a8fdf4f5bb1c9ce0dc3340a3d5e395d7`, checked 2026-09-20.

The user requested zero as the black-level default. Fresh editor profiles already
used zero, but runtime omission and legacy `AUTO` still assumed target white /
1000. Both now resolve to configured zero. The native handoff continues mapping
zero to `PL_COLOR_HDR_BLACK` (0.000001 nit), avoiding libplacebo's unspecified zero.

Explicit valid numeric black levels remain unchanged and inherit into child
profiles. An explicit `AUTO` resets an inherited measured value to zero. A
white-only override cannot change default black. Missing-file initialization,
loaded settings, profile overrides, final invalid-pair fallback, editor status,
help and reference examples now agree. SDR reference luminance is unchanged;
this setting is the HDR tone-mapping destination black level.

This supersedes the ratio-based remedy proposed for audit finding F7. Existing
files containing `AUTO` intentionally change from white/1000 to zero; the token
is retained rather than rewriting user files. Other VP-0128 findings remain open.

Validation:

- x64 Release solution build: passed, 0 errors, 47 warnings.
- Full Config editor suite: 76 passed, exit 0. Includes omitted/Auto/measured
  black, inherited white, profile switching, save/reload and existing validation.
- Native Config/Profile/Libplacebo filter: 357 tests; 356 passed initially.
  `ProfileChangeDisplayDurationIsBoundedAndLive` failed at the pre-existing
  immediate same-size file rewrite/reload assertion (ConfigFileTests.cpp:3190),
  then passed on its isolated rerun without any code changes. Both new black
  regressions passed in the initial run, including real libplacebo color-space
  inference at the native boundary.
- `git diff --check` passed. No deployment or active-config edits; no physical
  HDR/capture qualification. Editor physical two-monitor placement was skipped
  because only one monitor was available; synthetic placement coverage ran.

Logs: `artifacts/vp0128-zero-black/` (local build and test artifacts).
