# Verified local AR test deployment

Clean integration `4d1d62d0a664ba97137ba89497888bc8fad483a0` includes feature `c48eb134c34a93c762e026af6daf50aaeb1dc83c` and preserves installed24e. The full clean serial x64 Release solution Rebuild passed with exit0. All three genuine runtime identity sidecars, embedded exact commit identities and build hashes were verified.

Targeted native validation passed358/358, including the14 calibration tests and fallback-budget/crop/recovery coverage. The dedicated actual-capture regression passed1/1:64files,32distinct source identities,32repeats,two gaps, first full presentation10694 and logical full10700. Together these are359 test executions, not359 distinct tests. Captured pixels are not included.

The additional unfiltered full-DLL run stayed CPU-busy beyond eight minutes and produced no completed result. Root stopped its own VSTest process48764 after the user steered to local testing. `broad-suite-disposition.json` explicitly records interrupted-no-complete-result. Do not claim a full native-suite pass. The targeted checks qualified this local user-test deployment; broader qualification and live acceptance remain open.

Deployment at13:29:43 on2026-10-23 backed up and replaced the host, stable/Beta renderers and all three sidecars as one pinned runtime. Backup: `C:\Videoprocessor\vp\backup-vp0206-4d1d62d0-20261023-132943-579`. The player had already closed at the transaction check; the optional taskkill branch was not used. The updater gate and all-target exclusive handles guarded replacement. Playback was not restarted.

No configuration was edited. Active VideoProcessor.cfg SHA256 remains `5A744303135CFB76315BCE58DCDC0ED704B695B5E0CC904D5CF06A345170F97E`. Independent read-only verification rehashed all six deployed files against the new build and all six backup files against prior24e; all matched, as did the unchanged config. Dependencies and Config were preserved.

Draft unmerged [PR194](https://github.com/billslack2/videoprocessor/pull/194) remains ready for review and user live testing. No beta merge, GitHub release, full-suite completion or live jitter/quality acceptance is claimed. Earlier diagnosis/preflight/progress assets remain historical evidence; this receipt supersedes their pending build/deployment statements.
