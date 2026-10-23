# Accepted scope correction and verified beta merge

The user confirmed the deployed Scope improvement looks good and requested push and merge. Only [PR194](https://github.com/billslack2/videoprocessor/pull/194) was merged, into the discovered current/default beta `v1.4.02-beta` at `dfd34ea6f6365eddb17d986914bb7792836d694b`, from previous beta `f7c7a8204f7a2cace68b3b40774406680af6c0d8` and feature `c48eb134c34a93c762e026af6daf50aaeb1dc83c`.

The standalone feature completed a clean serial full x64 Release solution Rebuild,721/721 relevant native tests and1/1 actual64-file capture replay. Full presentation begins at10694 and logical full geometry at10700. This validates the fix directly on beta without depending on draft PR192/193. The merge's first parent and fetched beta tip were verified, and its tree `ffdbb4fca103909414207a792b22bc922f3055d8` exactly matches the tested standalone feature tree.

`merge-receipt.json`, `standalone-validation.json` and `verified-merge-summary.json` preserve exact pins, hashes and counters. Selected-test console logs, capture log/TRX and the reproducible test driver are included. The original build log remains local with its verified SHA256 in standalone-validation.json. The original1.03MB targeted TRX remains local; its SHA256, counters and all721 Passed outcomes were verified and preserved compactly. No binary or captured pixels are included.

The earlier optional unfiltered DLL sweep remains interrupted with no completed result. The selected721+1 executions do not turn that sweep into a full-suite pass. Prior diagnosis, controlled mixed CPU measurements and local deployment evidence remain part of the story.

Installed combined runtime `4d1d62d0a664ba97137ba89497888bc8fad483a0` remains distinct from merged beta. It preserves previously installed independent fixes; this merge did not redeploy binaries, change configuration, restart playback, merge PR192/193 or publish a release. User acceptance plus appropriate selected validation and confirmed beta merge close VP-0206; no exhaustive format/noise or live-jitter guarantee is claimed.
